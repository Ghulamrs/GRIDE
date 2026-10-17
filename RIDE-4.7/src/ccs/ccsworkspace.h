#ifndef EDITOR_CCS_WORKSPACE_H
#define EDITOR_CCS_WORKSPACE_H

#include <map>
#include <string>
#include <vector>

namespace editor {
namespace ccs {

// **A CCS workspace: a folder of projects, never a project itself.** RIDE opens one of its
// projects at a time, through <workspace>/<project>.pro, which holds only
// { "ccs": { "workspace": ".", "project": "<name>" } }. What is read here, as CCS 7.4 writes it
// (measured on the Windows box, 02-10-2026): the projects Eclipse has registered under
// .metadata/.plugins/org.eclipse.core.resources/.projects, each in the workspace folder or, when
// imported without copying, where its .location says; the path variables of
// org.eclipse.core.resources.prefs; and the build macros of org.eclipse.cdt.core.prefs.
struct Member {
    std::string name;       // as registered, which is .project's name
    std::string location;   // absolute, slashes one way
    bool inside = false;    // in the workspace folder rather than imported from elsewhere
};

struct Workspace {
    std::string dir;                                     // absolute, slashes one way
    std::vector<Member> projects;                        // CCS projects only, by name
    std::vector<std::string> notes;                      // a registered project missing, a location unread
    std::map<std::string, std::string> pathVariables;   // pathvariable.NAME, for linked resources
    std::map<std::string, std::string> macros;          // workspace build macros, for ${NAME}

    const Member* project(const std::string& name) const;
};

// Whether a folder is a CCS (Eclipse) workspace: it holds .metadata/.plugins/org.eclipse.core.resources.
bool isWorkspace(const std::string& dir);

// Reads the registry and the two preference files. False only when the folder is not a workspace.
bool readWorkspace(const std::string& dir, Workspace& out, std::string& error);

// The workspace a CCS project folder is registered in, by looking in its parent: "" when there is none.
std::string workspaceOf(const std::string& projectDir);

// The .pro that stands for one project of a workspace: <workspace>/<project>.pro.
std::string proFor(const std::string& workspaceDir, const std::string& project);

// Writes that .pro - workspace and project and nothing else - unless one is there already.
bool writePro(const std::string& workspaceDir, const std::string& project, std::string& file, std::string& error);

}
}

#endif
