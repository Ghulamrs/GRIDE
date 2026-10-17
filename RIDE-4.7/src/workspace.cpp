#include "workspace.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "path.h"

namespace editor {

namespace {

Outcome no(const std::string& why) {
    Outcome out;
    out.ok = false;
    out.message = why;
    return out;
}

Outcome yes(const std::string& said, const std::string& path = std::string()) {
    Outcome out;
    out.ok = true;
    out.message = said;
    out.path = path;
    return out;
}

std::string baseName(const std::string& path) {
    size_t at = path.find_last_of("/\\");
    return at == std::string::npos ? path : path.substr(at + 1);
}

Outcome andSave(Project& project, const std::string& said, const std::string& path) {
    if (!project.loaded()) return yes(said, path);
    std::string error;
    if (!project.save(error)) return no(error);
    return yes(said, path);
}

}

std::string withExtension(const Project& project, const std::string& relative,
                          ToolchainKind chosen) {
    std::string leaf = path::filename(relative);
    if (leaf.find('.') != std::string::npos) return relative;

    if (chosen == ToolCc1) return relative + ".c";
    if (chosen == ToolShc) return relative + ".shl";
    if (chosen == ToolCxx1 || chosen == ToolCxx || chosen == ToolMsvc) return relative + ".cpp";

    int counts[LangCount] = {0};
    const std::vector<Group>& groups = project.groups();
    for (size_t i = 0; i < groups.size(); ++i)
        for (size_t j = 0; j < groups[i].files.size(); ++j)
            counts[sourceLanguageFor(groups[i].files[j])]++;
    Language most = LangC;
    if (counts[LangCpp] > counts[most]) most = LangCpp;
    if (counts[LangShalimar] > counts[most]) most = LangShalimar;
    return relative + (most == LangCpp ? ".cpp" : most == LangShalimar ? ".shl" : ".c");
}

Outcome createFile(Project& project, const std::string& asked,
                   const std::string& group, ToolchainKind chosen) {
    std::string relative = withExtension(project, asked, chosen);
    std::string why;
    if (!Project::allows(relative, why)) return no(relative + ": " + why);

    std::string where = project.absolute(relative);
    if (path::exists(where)) return no(relative + " is already there");

    std::string parent = path::parent(where);
    if (!parent.empty()) path::makeDirectories(parent);

    FILE* made = std::fopen(where.c_str(), "wb");
    if (!made) return no("could not make " + relative);
    std::fclose(made);

    project.addFile(relative, group);
    return andSave(project, relative + " made" +
                   (relative != asked ? " - the extension picks the compiler, so one was added" : ""),
                   where);
}

// Inside a project the new name is relative to its root and the project
// follows the file; a file of no project, or one outside its root, is
// renamed where it is - a bare name stays beside it.
Outcome renameFile(Project& project, const std::string& fromAbsolute,
                   const std::string& toRelative) {
    if (fromAbsolute.empty()) return no("no file to rename");
    if (toRelative.empty()) return no("a file needs a name");

    bool inside = project.loaded() &&
                  project.groupOf(project.relative(fromAbsolute)) < project.groups().size();
    std::string to;
    if (inside) {
        std::string why;
        if (!Project::allows(toRelative, why)) return no(toRelative + ": " + why);
        to = project.absolute(toRelative);
    } else {
        std::string name = path::withSlashes(toRelative);
        bool rooted = name[0] == '/' || (name.size() > 1 && name[1] == ':');
        to = rooted ? name : path::join(path::parent(fromAbsolute), name);
    }
    if (path::exists(to)) return no(baseName(to) + " is already there");

    std::string parent = path::parent(to);
    if (!parent.empty()) path::makeDirectories(parent);

    if (!path::rename(fromAbsolute, to))
        return no("could not rename " + baseName(fromAbsolute) + " to " + baseName(to) +
                  " - is it open elsewhere, or read-only?");

    if (!inside) return yes(baseName(fromAbsolute) + " is now " + baseName(to), to);
    project.renameFile(project.relative(fromAbsolute), toRelative);
    return andSave(project, baseName(fromAbsolute) + " is now " + toRelative, to);
}

namespace {

bool endsWith(const std::string& name, const std::string& suffix) {
    if (name.size() < suffix.size()) return false;

    size_t at = name.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = name[at + i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

// A source of any of the three languages goes to Sources - the one group a project made here
// builds from. Shalimar had a group of its own until 2026-09-19, so a .shl made in the editor never
// reached the build; that Shalimar and C cannot share a group is the build's to state, not the pane's to prevent.
std::string groupForNamed(const std::string& name) {
    if (endsWith(name, ".h") || endsWith(name, ".hpp")) return "Headers";
    if (sourceLanguageFor(name) != LangPlain || endsWith(name, ".s")) return "Sources";

    return std::string();
}

bool worthDescending(const std::string& name) {
    if (name.empty() || name[0] == '.') return false;
    return name != "obj" && name != "build" && name != "x64" && name != "Debug" &&
           name != "Release" && name != "node_modules";
}

}

std::string groupForFile(const std::string& name) { return groupForNamed(name); }

Outcome beginFromWhatIsThere(Project& project, const std::string& directory) {
    std::string root = path::absolute(directory);
    project.begin(root, path::filename(root));

    project.addGroup("Headers");

    size_t found = 0;
    std::vector<path::Entry> here = path::entries(root);
    for (size_t i = 0; i < here.size(); ++i) {
        if (!here[i].directory) {
            std::string group = groupForNamed(here[i].name);
            if (group.empty()) continue;
            project.addFile(here[i].name, group);
            ++found;
            continue;
        }
        if (!worthDescending(here[i].name)) continue;

        std::vector<path::Entry> under = path::entries(path::join(root, here[i].name));
        for (size_t j = 0; j < under.size(); ++j) {
            if (under[j].directory) continue;
            std::string group = groupForNamed(under[j].name);
            if (group.empty()) continue;
            project.addFile(here[i].name + "/" + under[j].name, group);
            ++found;
        }
    }

    Outcome done = saveProject(project);
    if (!done.ok) return done;

    done.message = project.name() + " - no project here, so " + path::filename(project.file()) + " was made";
    if (found > 0) {
        char many[32];
        std::snprintf(many, sizeof many, "%lu", static_cast<unsigned long>(found));
        done.message += std::string(" with ") + many + " file" + (found == 1 ? "" : "s");
    }
    return done;
}

const char* const kDemoProgram =
    "#include <stdio.h>\n"
    "#include <math.h>\n"
    "\n"
    "/* Projectile motion - and something to try the debugger on.\n"
    "\n"
    "   F5 builds and runs it. F9 on a line puts a breakpoint there, F8 starts\n"
    "   it and stops on that line, and F7 steps a line at a time - watch the\n"
    "   Debug tab as rad, t_flight, h_max and range are worked out. */\n"
    "\n"
    "#ifndef M_PI\n"
    "#define M_PI 3.14   /* MSVC hides the real one behind _USE_MATH_DEFINES */\n"
    "#endif\n"
    "\n"
    "int main(void) {\n"
    "    double v0 = 20.0;     // initial velocity (m/s)\n"
    "    double angle = 45.0;  // launch angle (degrees)\n"
    "    double g = 9.81;      // gravity (m/s^2)\n"
    "\n"
    "    double rad = angle * M_PI / 180.0;\n"
    "    double t_flight = 2 * v0 * sin(rad) / g;\n"
    "    double h_max = (v0 * v0 * pow(sin(rad), 2)) / (2 * g);\n"
    "    double range = (v0 * v0 * sin(2 * rad)) / g;\n"
    "\n"
    "    printf(\"Time of flight: %.2f s\\n\", t_flight);\n"
    "    printf(\"Max height: %.2f m\\n\", h_max);\n"
    "    printf(\"Range: %.2f m\\n\", range);\n"
    "    return 0;\n"
    "}\n";

std::string demoDirectory() {
    std::string home = path::homeDir();
    if (home.empty()) return std::string();

    std::string dir = path::join(home, "c90-demo");
    std::string file = path::join(dir, "src/first.c");

    if (path::exists(file)) return dir;

    if (!path::makeDirectories(path::join(dir, "src"))) return std::string();

    FILE* out = std::fopen(file.c_str(), "wb");
    if (!out) return std::string();
    std::fwrite(kDemoProgram, 1, std::strlen(kDemoProgram), out);
    std::fclose(out);
    return dir;
}

Outcome deleteFile(Project& project, const std::string& absolute) {
    std::string relative = project.relative(absolute);

    if (!path::remove(absolute))
        return no("could not delete " + relative + " - it is still there");

    project.removeFile(relative);
    if (project.openFile() == relative) project.setOpenFile(std::string());
    return andSave(project, relative + " deleted", std::string());
}

Outcome moveToGroup(Project& project, const std::string& absolute,
                    const std::string& group) {
    std::string relative = project.relative(absolute);

    if (!project.moveToGroup(relative, group) && !project.addFile(relative, group))
        return no("could not move " + relative);

    return andSave(project, relative + " is in " + group, absolute);
}

Outcome addExisting(Project& project, const std::string& absolute,
                    const std::string& group) {
    std::string relative = project.relative(absolute);

    std::string why;
    if (!Project::allows(relative, why)) return no(relative + ": " + why);
    if (!project.addFile(relative, group)) return no(relative + " is already in the project");

    return andSave(project, relative + " added to " + group, absolute);
}

Outcome adoptSaved(Project& project, const std::string& absolute) {
    Outcome quiet;
    if (!project.loaded() || absolute.empty()) return quiet;

    std::string relative = project.relative(absolute);
    std::string why;
    if (!Project::allows(relative, why)) return quiet;
    if (project.groupOf(relative) < project.groups().size()) return quiet;
    if (path::filename(absolute) == path::filename(project.file()))
        return quiet;

    std::string group = groupForFile(path::filename(absolute));
    if (group.empty()) group = "Sources";
    project.addFile(relative, group);
    return andSave(project, relative + " saved, and added to " + group, absolute);
}

Outcome rememberOpen(Project& project, const std::string& absolute) {
    Outcome quiet;
    if (!project.loaded() || absolute.empty()) return quiet;
    std::string relative = project.relative(absolute);
    if (project.groupOf(relative) >= project.groups().size()) return quiet;
    if (relative == project.openFile()) return quiet;
    project.setOpenFile(relative);
    std::string error;
    quiet.ok = project.save(error);
    quiet.message = quiet.ok ? relative + " opens with the project from now on" : error;
    return quiet;
}

Outcome removeExisting(Project& project, const std::string& absolute) {
    std::string relative = project.relative(absolute);

    if (!project.removeFile(relative)) return no(relative + " is not in the project");
    // a file out of the project is not the one it opens with
    if (project.openFile() == relative) project.setOpenFile(std::string());

    return andSave(project, relative + " removed from the project - the file is still there",
                   std::string());
}

Outcome beginProject(Project& project, const std::string& directory,
                     const std::string& name, const std::string& firstFile) {
    // A directory not yet there is made: a new project may name its home
    // before its home exists.
    path::makeDirectories(path::absolute(directory));
    project.begin(directory, name);
    if (!firstFile.empty()) {
        std::string relative = project.relative(firstFile);
        std::string why;
        if (Project::allows(relative, why)) project.addFile(relative, "Sources");
    }
    return andSave(project, path::filename(project.file()) + " written - " + name,
                   project.file());
}

Outcome saveProject(Project& project) {
    if (!project.loaded()) return no("there is no project to save");
    // A CCS project's files are CCS's: what save() writes is RIDE's own state, elsewhere.
    if (project.isCcs()) return andSave(project, "RIDE's state for the CCS project written to settings.json - its own files are CCS's", project.file());
    return andSave(project, project.file() + " written", project.file());
}

}
