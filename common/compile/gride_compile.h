// gride_compile.h - the block: one piece of code that turns a project, its settings and toolchain.json into the
// commands of a build, for GRIDE's three outlooks. It plans and never runs: it starts no process, opens no
// window and keeps no state between calls. Design: common\Common_Compilation_Design.md.
//
// The DLL is compile.dll; this header is not compile.h because White's own runner is src\compile.h.
// The DLL and its callers are built together (same compiler, /MD) and never mixed from two releases: a caller
// compares version() with GRIDE_COMPILE_VERSION when it loads the DLL and refuses another number.
#ifndef GRIDE_COMPILE_H
#define GRIDE_COMPILE_H

#ifndef RC_INVOKED
#include <string>
#include <utility>
#include <vector>
#endif

#if defined(GRIDE_COMPILE_STATIC)      // the block's own tests take its sources in, with no DLL between
#define GRIDE_COMPILE_API
#elif defined(GRIDE_COMPILE_EXPORTS)
#define GRIDE_COMPILE_API __declspec(dllexport)
#else
#define GRIDE_COMPILE_API __declspec(dllimport)
#endif

#define GRIDE_COMPILE_MAJOR 1
#define GRIDE_COMPILE_MINOR 0
#define GRIDE_COMPILE_NUMBER "1.0"
#define GRIDE_COMPILE_VERSION "compile " GRIDE_COMPILE_NUMBER " - plans GRIDE's build commands"

// the resource compiler reads the numbers above for the files' version resources (compile.rc, gride-plan.rc), and
// nothing below
#ifndef RC_INVOKED

namespace gride { namespace compile {

// ---- what the block asks of its caller -------------------------------------------------------------------
// The planner makes no operating-system call. Files and the environment reach it through these two, so a
// test can give it a machine that is not there. systemFileSystem() and systemEnvironment() are this machine's.
// Every path is UTF-8.

struct FileSystem {
    virtual ~FileSystem() {}
    virtual bool isFile(const std::string &path) = 0;
    virtual bool isDirectory(const std::string &path) = 0;
    virtual bool read(const std::string &path, std::string &bytes) = 0;
    virtual bool write(const std::string &path, const std::string &bytes) = 0;     // makes the folders above it
    virtual long long modified(const std::string &path) = 0;                       // 0 when it is not there
    virtual std::vector<std::string> list(const std::string &folder) = 0;          // names; a folder's ends in '\'
};

struct Environment {
    virtual ~Environment() {}
    virtual std::string variable(const std::string &name) = 0;                     // "" when not set
    virtual std::string registry(const std::string &key, const std::string &value) = 0;   // HKLM; "" when absent
};

GRIDE_COMPILE_API FileSystem &systemFileSystem();
GRIDE_COMPILE_API Environment &systemEnvironment();

// The files a workspace is read from, and where GRIDE.exe is. defaultPaths() gives the installed layout:
// toolchain.json beside the program, the user's toolchain.json and settings.json in %APPDATA%\GRIDE.
struct Paths {
    std::string shippedToolchain;     // <program folder>\toolchain.json
    std::string userToolchain;        // %APPDATA%\GRIDE\toolchain.json - may be absent
    std::string settings;             // %APPDATA%\GRIDE\settings.json - may be absent
    std::string programFolder;        // where GRIDE.exe and the tools are
    std::string outlook;              // "Black", "Grey", "White", or "" - whose old settings keys are read first
};

GRIDE_COMPILE_API Paths defaultPaths(const std::string &programFolder, const std::string &outlook);

// ---- a request and its plan ------------------------------------------------------------------------------

enum Action { Build, Rebuild, Clean, CompileFile, Run, Emulate, Simulate, Verify, Debug, Convert };

struct Request {
    std::string project;              // the .pro, full path
    std::string configuration;        // "Debug" or "Release"
    std::string target;               // toolchain.json's arch (x86_64-windows, tms6747 ...) or its ideTarget
    Action action;
    std::string file;                 // CompileFile: the source
    std::vector<std::string> arguments;
    bool native;                      // the user's yes to the native-tools question (Plan::asksNative)
    std::string workspaceFolder;      // Black's workspace folder, for $(WorkspaceDir); "" is the project's
    Request() : action(Build), native(false) {}
};

struct Problem {
    std::string severity;             // "error" | "warning" | "note"
    std::string file;                 // the file it is about, or ""
    std::string text;
};

struct Step {
    std::string kind;                 // compile | assemble | link
    std::string tool;                 // toolchain.json's id (c90, masm, link ...)
    std::string program;              // the path found; the tool's file name when it was not found
    std::vector<std::string> argv;    // one element per argument, unquoted; argv is the plan
    std::string directory;            // where it runs
    std::vector<std::pair<std::string, std::string> > environment;   // this step's own additions, nothing else
    std::vector<std::string> inputs, outputs;
    std::string stamp;                // program, argv and environment, hashed: a changed stamp means run again
    std::string encoding;             // what the tool writes: "utf-8" | "oem"
    std::string diagnostics;          // the dialect of its messages (toolchain.json's diagnostics)
    std::string shown;                // what the step is about, for the log: "main.c", "main.s, account.s"
    std::string why;                  // which rule of the design's section 4 chose this tool, in words
    int wave;                         // steps of one wave may run together; waves run in order
    Step() : wave(0) {}
};

// what one entry of a group gave (the design's 3.4), for the log's "files" line and the explorers
struct Listed {
    std::string group, entry;         // "Sources", "*.cpp"
    bool pattern;                     // the entry has a * or a ?
    std::vector<std::string> files;   // relative to the project, '/' between folders, in build order
    Listed() : pattern(false) {}
};

struct Plan {
    std::vector<Step> steps;
    std::vector<std::pair<std::string, std::string> > files;   // files to write before the first step: path, text
    std::vector<Problem> problems;    // an "error" among them: the plan is not to be run
    bool asksNative;                  // the native-tools question applies to this build
    std::string program;              // what the link writes
    std::string outputFolder, objectFolder;
    std::vector<std::string> cleans;  // Clean and Rebuild: the folders to empty first
    std::vector<Listed> listed;       // the build's groups, entry by entry
    std::vector<std::string> notes;   // what the block decided and an outlook may show ("Microsoft's runtime is linked ...")
    Plan() : asksNative(false) {}
    bool runnable() const {
        for (size_t i = 0; i < problems.size(); ++i) if (problems[i].severity == "error") return false;
        return true;
    }
};

// ---- the workspace ---------------------------------------------------------------------------------------
// toolchain.json (shipped, with the user's changes merged in) and the build settings, read once by load().
// A project is read afresh by every plan(). Only this class and the functions below are exported; the records
// above are plain structures.

struct WorkspaceData;      // the two classes' data, each behind one pointer; defined inside the block
struct BuildLogData;

class GRIDE_COMPILE_API Workspace {
public:
    static Workspace load(const Paths &, FileSystem &, Environment &);    // the two must outlive the workspace
    std::vector<Problem> problems() const;                 // what load() found wrong (an unreadable file ...)
    Plan plan(const Request &) const;
    std::vector<Listed> files(const std::string &project) const;         // every group, entry by entry
    std::string describe() const;                          // "compile 1.0, toolchain.json as shipped; from <folder>"
    // steps 6 to 8 of the design add here: settings(), compilerOptions(), tools(), change()
    Workspace(const Workspace &);
    Workspace &operator=(const Workspace &);
    ~Workspace();
private:
    friend class BuildLog;
    friend class Stamps;
    Workspace();
    WorkspaceData *data_;
};

// ---- the build log (the design's 5.1) --------------------------------------------------------------------
// Planned whole from a plan before anything runs, then filled in the plan's order whatever order the steps end
// in. The three outlooks print what these functions return and add nothing.

class GRIDE_COMPILE_API BuildLog {
public:
    BuildLog(const Workspace &, const Request &, const Plan &, const std::string &outlook, const std::string &when);
    std::string planned() const;                     // the complete log before anything runs
    std::string dryRun() const;                      // the build as a script: "DRY RUN ONLY", then every command whole -
                                                     // a plan holds every step, whatever is up to date
    void showCommands(bool onScreen);                // whether the lines handed back hold each command line (the kept
                                                     // text always does; a step that fails shows its command anyway)
    std::vector<std::string> begin();                // the head
    std::vector<std::string> finished(int step, int exitCode, const std::string &output, double seconds);
                                                     // a step ended: the lines now due, in order - none while an
                                                     // earlier step is still running, several once it has ended
    std::vector<std::string> upToDate(int step);     // a step that did not need to run
    std::vector<std::string> end(double elapsed = -1);   // the closing lines; `elapsed`: the build's seconds by the clock
                                                     // (the block reads none) - without it, the steps' own, added up
    std::string text() const;                        // everything released so far
    std::string file() const;                        // obj\<Config>\<Target>\<project>.build.log
    int errors() const;
    int warnings() const;
    ~BuildLog();
private:
    BuildLog(const BuildLog &);
    BuildLog &operator=(const BuildLog &);
    BuildLogData *data_;
};

// ---- which steps need to run (the design's section 5) ---------------------------------------------------
// A step is up to date when its stamp is the one of its last run that succeeded, what it writes is there, and
// nothing it reads is newer than what it writes. The stamps are kept beside the objects, in <project>.stamps.
// Asked in the plan's order: a step that ran rewrites what the next one reads.

struct StampsData;

class GRIDE_COMPILE_API Stamps {
public:
    Stamps(const Workspace &, const Request &, const Plan &);
    bool upToDate(int step) const;
    void ran(int step, bool succeeded);              // remembered when it succeeded, forgotten when it failed
    void save();
    ~Stamps();
private:
    Stamps(const Stamps &);
    Stamps &operator=(const Stamps &);
    StampsData *data_;
};

GRIDE_COMPILE_API const char *version();          // GRIDE_COMPILE_VERSION, as the DLL was built

// One command line from a program and its arguments, by Windows' own quoting rules: the one place a plan's
// argv becomes text. splitCommandLine is its inverse.
GRIDE_COMPILE_API std::string windowsCommandLine(const std::string &program, const std::vector<std::string> &argv);
GRIDE_COMPILE_API std::vector<std::string> splitCommandLine(const std::string &line);

}}

#endif // RC_INVOKED

#endif
