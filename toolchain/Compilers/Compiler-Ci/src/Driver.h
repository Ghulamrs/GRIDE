#pragma once

#include "backend/Backend.h"

#include <string>
#include <vector>

class Driver {
public:
    int run(int argc, char **argv);

    static const char *bannerLine();
    // After run() returned 0: whether to say so - a compile was begun and -nologo was not given.
    bool saysDone() const { return saysDone_; }
    // What this run made, for the line above the one that says it finished: the program, the objects or the assembly.
    std::vector<std::string> produced() const;

private:
    struct Job {
        std::string input;
        std::string output;
    };

    std::string program_;
    std::vector<Job> jobs_;
    std::vector<std::string> searchPath_;
    const Backend *backend_ = &defaultBackend();
    // Set when --version was answered, so run() leaves with 0 rather than
    // reporting a usage error the caller did not make.
    bool answered_ = false;
    bool quiet_ = false;   // -nologo: leave out the start-of-compile banner
    bool saysDone_ = false;  // a compile was begun, and the line saying it finished is wanted
    bool toStdout_ = false;
    bool timing_ = false;
    bool assemblyOnly_ = false;
    bool debug_ = false;
    // -O0, -O1 or -O2: how hard the code generator optimizes.
    int optimize_ = 0;
    bool objectOnly_ = false;
    unsigned threads_ = 0;
    std::string linkTo_;
    std::vector<std::string> temporaries_;
    std::vector<std::string> objects_;

    struct MacroEdit {
        std::string name;
        std::string value;
        bool undef;
    };
    std::vector<MacroEdit> macroEdits_;

    bool parseArguments(int argc, char **argv);

    bool compile(const Job &job);

    bool runJobs();
    unsigned threadCount() const;
    bool link();
    bool assembleObjects();
    void removeTemporaries();
    std::vector<std::pair<std::string, std::string> > macrosFor() const;
    void addMacroEdit(const char *text, bool undef);

    static unsigned availableCores();

    static std::string assemblyNameFor(const std::string &source);
    std::string objectNameFor(const std::string &source) const;
    static std::string temporaryName(int index);
    static const char *hostCompiler();
    static const char *hostAssembler();
    static const char *hostLinker();
    static const char *hostGnuAssembler();
    static std::string windowsAssemble(const std::string &source, const std::string &object);
    // **The tms6747 target is assembled and linked on any host**: by asm6x,
    // the project's own C6000 assembler, and by TI's lnk6x where CCS is.
    bool targetIsTi() const;
    std::string tiAssembler() const;
    std::string tiLinker() const;
    bool linkTi();
    static void usage(char *);
    void standardIncludeDirectory(const std::string &argv0);
};
