#ifndef EDITOR_WORKSPACE_H
#define EDITOR_WORKSPACE_H

#include <string>

#include "project.h"

namespace editor {

struct Outcome {
    bool ok = false;
    std::string message;
    std::string path;
};

// A name with no extension is given one, since the extension is what picks the compiler: the
// chosen compiler's - the one the menu bar's corner shows - or, with the choice on automatic,
// the one most of the project's sources have, else .c.
std::string withExtension(const Project& project, const std::string& relative,
                          ToolchainKind chosen = ToolAuto);

Outcome createFile(Project& project, const std::string& relative,
                   const std::string& group, ToolchainKind chosen = ToolAuto);

Outcome renameFile(Project& project, const std::string& fromAbsolute,
                   const std::string& toRelative);

Outcome deleteFile(Project& project, const std::string& absolute);

Outcome moveToGroup(Project& project, const std::string& absolute,
                    const std::string& group);

std::string groupForFile(const std::string& name);

// A file saved under the project's root joins it, in the group its name puts it in, unless already there or where a project file may not lie; nothing is said when it does not apply.
Outcome adoptSaved(Project& project, const std::string& absolute);

// The file in front when the project is closed or left opens next time: written into the project when it is one of its files and not already recorded. Nothing is said either way.
Outcome rememberOpen(Project& project, const std::string& absolute);

Outcome addExisting(Project& project, const std::string& absolute,
                    const std::string& group);

Outcome removeExisting(Project& project, const std::string& absolute);

Outcome beginProject(Project& project, const std::string& directory,
                     const std::string& name, const std::string& firstFile);
Outcome saveProject(Project& project);

Outcome beginFromWhatIsThere(Project& project, const std::string& directory);

std::string demoDirectory();

}

#endif
