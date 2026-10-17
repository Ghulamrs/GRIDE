#ifndef EDITOR_CCS_OPTIONS_H
#define EDITOR_CCS_OPTIONS_H

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace editor {
namespace ccs {

// **TI's own option definitions, read as data.** CCS stores in .cproject only what differs from
// the definition's default, so what a project means by an option it does not mention - Release's
// -O2, the linker's --rom_model - is in the definition and nowhere else, and so is how an
// enumerated value is spelled on the command line. docs/ccs-reference/ti-option-definitions/
// options-ccs74-C6000_8.2.tsv and options-ccs55-C6000_7.4.tsv are those definitions, one row per
// option, made from the XML CCS ships; the two files travel with RIDE in lib/ccs.
struct OptionDef {
    std::string id;           // the tail after the version prefix: compilerID.OPT_LEVEL
    std::string valueType;    // string, boolean, enumerated, stringList, definedSymbols, includePath, libPaths, libs
    std::string command;      // --heap_size=, -O, --define=
    std::string defaultValue; // a plain default, or an enumerated one's full id
    std::string superClass;   // OPT_LEVEL.release's is OPT_LEVEL
    std::vector<std::pair<std::string, std::string> > enums;   // value suffix -> flag
    std::string defaultEnum;  // the suffix marked (default), or ""
};

class OptionDefs {
public:
    // Loads the file for a compiler version - "8.2.2" reads the 8.2 file, "7.4.4" the 7.4 one,
    // anything else the nearest major. False, with a reason, when the file is not there.
    bool load(const std::string& version, std::string& why);
    bool loaded() const { return !defs_.empty(); }
    const std::string& file() const { return file_; }
    // By the id tail, or the superClass tail an instance was derived from.
    const OptionDef* find(const std::string& idTail) const;
    // The command-line spelling of a stored value, from the definition: a boolean's command, a
    // string's command and value, an enumerated value's own flag, a list's command per item.
    std::string spell(const std::string& idTail, const std::string& value,
                      const std::vector<std::string>& items) const;
    // The enumerated default's suffix, or the plain defaultValue, for an option the project left unstored.
    std::string defaultOf(const std::string& idTail) const;
private:
    std::map<std::string, OptionDef> defs_;
    std::string file_;
};

// Where the two TSV files are: lib/ccs beside the program's directory, or the checkout's
// docs/ccs-reference/ti-option-definitions, whichever holds them; a suite names one outright.
std::string definitionsDir();
void setDefinitionsDir(const std::string& dir);

// "com.ti.ccstudio.buildDefinitions.C6000_8.2.compilerID.OPT_LEVEL" -> "compilerID.OPT_LEVEL";
// an enumerated value's id -> its suffix after the option's own id.
std::string idTail(const std::string& fullId);
std::string enumSuffix(const std::string& optionId, const std::string& valueId);

}
}

#endif
