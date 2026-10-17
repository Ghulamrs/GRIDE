#ifndef EDITOR_PROJECT_H
#define EDITOR_PROJECT_H

#include <string>
#include <vector>

#include "ccs/ccsproject.h"
#include "indent.h"
#include "options.h"
#include "toolchain.h"

namespace editor {

struct Group {
    std::string name;
    std::vector<std::string> files;

    ToolchainKind toolchain;

    Group() : toolchain(ToolAuto) {}
};

struct Part {
    std::string group;
    ToolchainKind toolchain;
    Language lang;
    std::vector<std::string> sources;

    Part() : toolchain(ToolAuto), lang(LangPlain) {}
};

struct Target {
    std::string name;
    std::vector<std::string> groups;
    /*  What to pass the program when it is run. A project whose target is a
     *  tool rather than a demonstration needs something to work on - the
     *  compilerpp project is a compiler, and Run with nothing after it only
     *  ever printed that compiler's usage. Each entry is one argument,
     *  already split, so a path with a space in it stays one argument. A
     *  relative path is taken from the project's root, which is where the
     *  program is run from. */
    std::vector<std::string> args;
};

ToolchainKind toolchainOf(const Toolchain& tool, const Part& part);

class Project {
public:
    Project();
    ~Project();

    static std::string fileIn(const std::string& directory);

    static const char* suffix();

    static std::vector<std::string> projectFilesIn(const std::string& directory);

    bool load(const std::string& dir, std::string& error);
    bool save(std::string& error);

    bool saveAs(const std::string& file, std::string& error);

    bool loaded() const { return loaded_; }
    const std::string& root() const { return root_; }

    void setRoot(const std::string& path) { root_ = path; }
    const std::string& file() const { return file_; }
    const std::string& name() const { return name_; }
    void setName(const std::string& name) { name_ = name; }

    const std::vector<Group>& groups() const { return groups_; }

    const Target& target() const { return target_; }
    bool builds() const { return !target_.groups.empty(); }
    void setTarget(const Target& target) { target_ = target; }

    bool targetSources(std::vector<std::string>& sources, Language& lang,
                       std::string& why, std::string* detail = 0) const;
    // **Run file on one source of a several-source build runs the project**: alone it links against nothing. Answers the count of sources the file is one of, or 0 when it stands alone.
    size_t runsAsProject(const std::string& file) const;

    bool targetParts(std::vector<Part>& parts, std::string& why,
                     std::string* detail = 0) const;

    ToolchainKind toolchainFor(const std::string& group) const;
    void setGroupToolchain(const std::string& group, ToolchainKind kind);

    static std::string stemOf(const std::string& leaf);

    std::string targetProgram() const;
    /*  The run arguments, as written; and resolved against the root, which
     *  is what actually reaches the program. */
    const std::vector<std::string>& targetArgs() const { return target_.args; }
    std::vector<std::string> absoluteTargetArgs() const;
    void setTargetArgs(const std::vector<std::string>& args) { target_.args = args; }
    // The program's command line as one line of text - an argument with a space in it in quotes - and back:
    // kept in the .pro, or for a CCS project in RIDE's own state, never in CCS's files.
    std::string argumentsText() const;
    bool rememberArguments(const std::string& line, std::string& error);
    static std::vector<std::string> splitArguments(const std::string& line);
    const IndentStyle& indent() const { return indent_; }
    ToolchainKind toolchain() const { return toolchain_; }
    const std::string& arch() const { return arch_; }

    // Header directories the project's sources include from, and libraries
    // its program links, each relative to the root as written in the file
    // (an absolute one stays absolute); the second form has them resolved.
    const std::vector<std::string>& includes() const { return includes_; }
    const std::vector<std::string>& libraries() const { return libraries_; }
    void setIncludes(const std::vector<std::string>& dirs) { includes_ = dirs; }
    void setLibraries(const std::vector<std::string>& files) { libraries_ = files; }
    std::vector<std::string> absoluteIncludes() const;
    std::vector<std::string> absoluteLibraries() const;

    // The file to open with the project, relative to the root: what the file
    // says, else the one that defines main, else the first there is. Empty
    // when the project holds no file at all.
    const std::string& openFile() const { return open_; }
    // "emulateOnly": true in the .pro - a tms6747 build with no .out, its Run the emulator's (D2).
    bool emulateOnly() const { return emulateOnly_; }
    void setOpenFile(const std::string& relative) { open_ = relative; }
    std::string fileToOpen() const;
    std::string mainFile() const;

    // **Compiler Options** (options.h): per configuration, in the .pro under "options", and the
    // store every build reads while the project is open.
    const options::Store& compilerOptions() const { return options_; }
    void setCompilerOptions(const options::Store& store) { options_ = store; }

    // **A CCS project opened as it is** (ccs/ccsproject.h): read afresh at every open and every
    // build, never written - save() keeps RIDE's own state in settings.json - and its options
    // read-only. isCcs() says so; the rest answer what was read.
    bool isCcs() const { return ccs_; }
    // The CCS workspace the project was opened from through <workspace>/<project>.pro, or "".
    const std::string& ccsWorkspace() const { return ccsWorkspace_; }
    const ccs::Reading& ccsReading() const { return ccsReading_; }
    // The Messages lines for a configuration: the options line, then the sources line when there is one.
    std::vector<std::string> ccsReport(Configuration config) const;
    std::string ccsMapping(Configuration config) const;
    // What lnk6x is handed for this configuration; not given for a .pro project.
    TiLink tiLink(Configuration config) const;
    // The configuration settings.json remembers for this CCS project: 0 Debug, 1 Release, -1 unsaid.
    int ccsConfiguration() const;
    bool rememberConfiguration(Configuration config);
    // Reads the three CCS files again; a .pro project is left as it is. False, with the reason, when they no longer read.
    bool reloadIfCcs(std::string& error);

    void setIndent(const IndentStyle& style) { indent_ = style; indentSaid_ = true; }
    void setToolchain(ToolchainKind kind) { toolchain_ = kind; }
    void setArch(const std::string& arch) { arch_ = arch; }

    void begin(const std::string& dir, const std::string& name);

    static bool allows(const std::string& relative, std::string& why);

    std::vector<std::string> directories() const;

    void close();

    void addGroup(const std::string& group);
    bool addFile(const std::string& relative, const std::string& group);
    bool removeFile(const std::string& relative);
    bool renameFile(const std::string& from, const std::string& to);
    bool moveToGroup(const std::string& relative, const std::string& group);

    size_t groupOf(const std::string& relative) const;

    std::string absolute(const std::string& relative) const;
    std::string relative(const std::string& path) const;

private:
    bool loaded_;
    std::string root_;
    std::string file_;
    std::string name_;
    std::vector<Group> groups_;
    Target target_;
    IndentStyle indent_;

    bool oneShalimarProgram(std::vector<std::string>& sources, std::string& why,
                            std::string* detail) const;
    ToolchainKind toolchain_;
    std::string arch_;
    std::vector<std::string> includes_;
    std::vector<std::string> libraries_;
    std::string open_;
    bool emulateOnly_ = false;
    // Whether the file names its own indentation; unsaid, the
    // installation's settings.json answers and nothing is written.
    bool indentSaid_ = false;
    options::Store options_;
    bool ccs_ = false;
    ccs::Reading ccsReading_;
    std::string ccsWorkspace_;
    bool loadCcs(const std::string& dir, std::string& error, const std::string& workspace = std::string());
    // A .pro of one CCS workspace project: { "ccs": { "workspace": ".", "project": "<name>" } }.
    bool loadWorkspacePro(const std::string& pro, const Json& ccs, std::string& error);
};

}

#endif
