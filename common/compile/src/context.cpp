#include "context.h"

#include <cstdlib>

namespace gride { namespace compile {

Words VisualStudio::binDirs() const {
    Words r;
    if (!msvcBin.empty()) r.push_back(msvcBin);
    if (!llvmBin.empty()) r.push_back(llvmBin);
    if (!kitBin.empty()) r.push_back(kitBin);
    return r;
}

Words VisualStudio::libDirs() const {
    Words r;
    if (!msvcLib.empty()) r.push_back(msvcLib);
    for (size_t i = 0; i < kitLibs.size(); ++i) r.push_back(kitLibs[i]);
    return r;
}

Words VisualStudio::includeDirs() const {
    Words r;
    if (!msvcInclude.empty()) r.push_back(msvcInclude);
    for (size_t i = 0; i < kitIncludes.size(); ++i) r.push_back(kitIncludes[i]);
    return r;
}

Context::Context(const WorkspaceData &w)
    : defs(w.defs), settings(w.settings), fs(*w.fs), env(*w.env), programFolder(tidy(w.paths.programFolder)),
      lookedBin_(false), lookedVs_(false), lookedTi_(false) {}

std::string Context::expand(const std::string &text) const {
    std::string r;
    for (size_t i = 0; i < text.size();) {
        size_t end = text[i] == '%' ? text.find('%', i + 1) : std::string::npos;
        if (end != std::string::npos && end > i + 1) {
            std::string value = env.variable(text.substr(i + 1, end - i - 1));
            if (!value.empty()) { r += value; i = end + 1; continue; }
        }
        r += text[i++];
    }
    return r;
}

bool Context::hasMarker(const std::string &folder) const {
    for (size_t i = 0; i < defs.installMarkers.size(); ++i)
        if (fs.isFile(combine(folder, defs.installMarkers[i]))) return true;
    return false;
}

// the installation's bin: the root the settings name, else the file's default roots; a folder counts when one
// of the markers is in it or in its bin folder
std::string Context::installBin() const {
    if (lookedBin_) return bin_;
    lookedBin_ = true;
    Words roots;
    if (!settings.root.empty()) roots.push_back(expand(settings.root));
    for (size_t i = 0; i < defs.installRoots.size(); ++i) {
        std::string x = expand(defs.installRoots[i]);
        if (x.find('%') == std::string::npos) addOnce(roots, x);
    }
    for (size_t i = 0; i < roots.size(); ++i) {
        if (hasMarker(roots[i])) { bin_ = tidy(roots[i]); break; }
        std::string bin = defs.installBin.empty() ? roots[i] : combine(roots[i], defs.installBin);
        if (hasMarker(bin)) { bin_ = bin; break; }
    }
    return bin_;
}

std::string Context::installRoot() const {
    std::string bin = installBin();
    if (bin.empty() || defs.installBin.empty()) return bin;
    return same(leaf(bin), defs.installBin) ? parent(bin) : bin;
}

// one of the installation's own folders: the one the settings name, made absolute against the installation,
// else the folder of that name under it; "" when neither is there
std::string Context::installDir(const std::string &which, const std::string &namedInSettings) const {
    std::string root = installRoot();
    if (!namedInSettings.empty()) {
        std::string n = expand(namedInSettings);
        std::string dir = absolute(n) || root.empty() ? tidy(n) : combine(root, n);
        return fs.isDirectory(dir) ? dir : std::string();
    }
    if (root.empty() || which.empty()) return std::string();
    std::string dir = combine(root, which);
    return fs.isDirectory(dir) ? dir : std::string();
}

Words Context::searchDirs() const {
    Words r;
    if (!programFolder.empty()) r.push_back(programFolder);
    std::string bin = installBin();
    if (!bin.empty()) addOnce(r, bin);
    return r;
}

namespace {

std::vector<long> numbers(const std::string &name) {
    std::vector<long> r;
    Words parts = split(name, ".", true);
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].empty() || parts[i].find_first_not_of("0123456789") != std::string::npos) return std::vector<long>();
        r.push_back(std::atol(parts[i].c_str()));
    }
    return r;
}

}

std::string Context::newestVersionUnder(const std::string &folder, const std::string &prefix) const {
    Words names = fs.list(folder);
    std::string best;
    std::vector<long> bestN;
    for (size_t i = 0; i < names.size(); ++i) {
        std::string n = names[i];
        if (n.empty() || n[n.size() - 1] != '\\') continue;
        n.erase(n.size() - 1);
        if (!prefix.empty() && !startsWith(lower(n), lower(prefix))) continue;
        std::vector<long> v = numbers(n.substr(prefix.size()));
        if (v.empty()) continue;
        if (best.empty() || v > bestN) { best = n; bestN = v; }
    }
    return best;
}

// Visual Studio, found without starting a program: the vcvars64.bat the settings name, else the file's default
// roots and editions. (Black also asked vswhere.exe; the block starts no process, and the default roots cover
// what vswhere found on an ordinary installation.)
const VisualStudio &Context::visualStudio() const {
    if (lookedVs_) return vs_;
    lookedVs_ = true;
    const VisualStudioDef &d = defs.visualStudio;
    if (d.msvcTools.empty()) return vs_;
    std::string root;
    if (!settings.vcvars.empty() && !d.vcvars.empty()) {
        std::string named = tidy(expand(settings.vcvars));
        if (fs.isFile(named) && endsWith(named, slashes(d.vcvars))) root = named.substr(0, named.size() - d.vcvars.size() - 1);
    }
    for (size_t r = 0; root.empty() && r < d.defaultRoots.size(); ++r) {
        std::string base = expand(d.defaultRoots[r]);
        if (base.find('%') != std::string::npos) continue;
        for (size_t e = 0; root.empty() && e < d.editions.size(); ++e) {
            std::string candidate = combine(base, d.editions[e]);
            if (fs.isDirectory(combine(candidate, d.msvcTools))) root = candidate;
        }
    }
    if (root.empty()) return vs_;
    std::string tools = combine(root, d.msvcTools);
    std::string version = !d.msvcVersion.empty() && fs.isDirectory(combine(tools, d.msvcVersion)) ? d.msvcVersion : newestVersionUnder(tools);
    if (version.empty()) return vs_;
    vs_.found = true;
    vs_.root = root;
    vs_.msvcVersion = version;
    std::string msvc = combine(tools, version);
    vs_.msvcBin = combine(msvc, d.hostBin);
    vs_.msvcLib = combine(msvc, d.lib);
    vs_.msvcInclude = combine(msvc, d.include);
    std::string llvm = combine(root, d.llvmBin);
    if (!d.llvmBin.empty() && fs.isDirectory(llvm)) vs_.llvmBin = llvm;

    Words kitRoots;
    if (!d.kitRegistryKey.empty()) {
        std::string named = env.registry(d.kitRegistryKey, d.kitRegistryValue);
        if (!named.empty()) kitRoots.push_back(tidy(named));
    }
    for (size_t i = 0; i < d.kitRoots.size(); ++i) addOnce(kitRoots, tidy(expand(d.kitRoots[i])));
    for (size_t i = 0; i < kitRoots.size() && vs_.kitRoot.empty(); ++i) {
        std::string v = newestVersionUnder(combine(kitRoots[i], d.kitVersionsUnder));
        if (v.empty()) continue;
        vs_.kitRoot = kitRoots[i];
        vs_.kitVersion = v;
        for (size_t k = 0; k < d.kitLibDirs.size(); ++k) {
            std::string dir = combine(kitRoots[i], replaceAll(d.kitLibDirs[k], "$(Version)", v));
            if (fs.isDirectory(dir)) vs_.kitLibs.push_back(dir);
        }
        for (size_t k = 0; k < d.kitIncludeDirs.size(); ++k) {
            std::string dir = combine(kitRoots[i], replaceAll(d.kitIncludeDirs[k], "$(Version)", v));
            if (fs.isDirectory(dir)) vs_.kitIncludes.push_back(dir);
        }
        std::string bin = combine(kitRoots[i], replaceAll(d.kitBinDir, "$(Version)", v));
        if (fs.isDirectory(bin)) vs_.kitBin = bin;
    }
    return vs_;
}

// TI's own compiler directory: the one the settings name when it is there, else the newest that CCS's
// installer left under the roots the file names
std::string Context::tiDir() const {
    if (lookedTi_) return ti_;
    lookedTi_ = true;
    if (!settings.ti.empty()) {
        std::string named = tidy(expand(settings.ti));
        if (fs.isDirectory(named)) { ti_ = named; return ti_; }
    }
    std::vector<long> bestN;
    for (size_t r = 0; r < defs.tiRoots.size(); ++r) {
        std::string root = tidy(expand(defs.tiRoots[r]));
        for (size_t u = 0; u < defs.tiUnder.size(); ++u) {
            // "ccs*\tools\compiler": the one component with a * is every folder that matches it
            Words folders(1, root), parts = split(slashes(defs.tiUnder[u]), "\\");
            for (size_t p = 0; p < parts.size(); ++p) {
                Words next;
                for (size_t f = 0; f < folders.size(); ++f) {
                    if (!wildcard(parts[p])) { next.push_back(combine(folders[f], parts[p])); continue; }
                    Words names = fs.list(folders[f]);
                    for (size_t n = 0; n < names.size(); ++n) {
                        std::string name = names[n];
                        if (name.empty() || name[name.size() - 1] != '\\') continue;
                        name.erase(name.size() - 1);
                        if (matches(parts[p], name)) next.push_back(combine(folders[f], name));
                    }
                }
                folders = next;
            }
            for (size_t f = 0; f < folders.size(); ++f)
                for (Named::const_iterator it = defs.tiCompilerDirs.begin(); it != defs.tiCompilerDirs.end(); ++it) {
                    std::string v = newestVersionUnder(folders[f], it->second);
                    if (v.empty()) continue;
                    std::vector<long> n = numbers(v.substr(it->second.size()));
                    if (ti_.empty() || n > bestN) { ti_ = combine(folders[f], v); bestN = n; }
                }
        }
    }
    return ti_;
}

// A tool by its definition, in the order its lookup gives: the environment variable; beside GRIDE.exe and the
// installation's bin when it is the installation's (installDir named); Visual Studio's folders when it is
// reached through vcvars; TI's bin for TI's own tools; the lookup's own folders; PATH when the lookup allows it.
std::string Context::findTool(const ToolDef &tool, const TargetDef *target, Words *looked) const {
    Words names = tool.names();
    if (!tool.lookupEnv.empty()) {
        std::string named = env.variable(tool.lookupEnv);
        if (!named.empty() && fs.isFile(named)) return tidy(named);
        if (looked) looked->push_back("%" + tool.lookupEnv + "%" + (named.empty() ? " (not set)" : " = " + named));
    }
    Words dirs, said;
    if (!tool.lookupInstallDir.empty()) {
        if (!programFolder.empty()) { dirs.push_back(programFolder); said.push_back("beside GRIDE.exe (" + programFolder + ")"); }
        std::string bin = installBin();
        if (!bin.empty() && !contains(dirs, bin)) { dirs.push_back(bin); said.push_back("the installation's " + bin); }
    }
    if (tool.lookupVcvars) {
        const VisualStudio &vs = visualStudio();
        Words bins = vs.binDirs();
        for (size_t i = 0; i < bins.size(); ++i) dirs.push_back(bins[i]);
        said.push_back(vs.found ? "Visual Studio's " + join(bins, ", ") : "Visual Studio (none found)");
    }
    bool tiTool = target && target->emulated && (tool.role == "nativeLinker" || tool.role == "nativeAssembler");
    if (tiTool) {
        std::string ti = tiDir();
        std::string bin = ti.empty() ? std::string() : defs.tiBin.empty() ? ti : combine(ti, defs.tiBin);
        if (!bin.empty() && fs.isDirectory(bin)) { dirs.push_back(bin); said.push_back("TI's compiler directory's " + bin); }
        else said.push_back("TI's compiler directory (none named in the settings, none found under " + join(defs.tiRoots, ", ") + ")");
    }
    for (size_t i = 0; i < tool.lookupDirs.size(); ++i) { dirs.push_back(tidy(expand(tool.lookupDirs[i]))); said.push_back(dirs.back()); }
    for (size_t d = 0; d < dirs.size(); ++d)
        for (size_t n = 0; n < names.size(); ++n) {
            std::string full = combine(dirs[d], names[n]);
            if (fs.isFile(full)) return full;
        }
    if (looked) for (size_t i = 0; i < said.size(); ++i) looked->push_back(said[i]);
    if (!tool.lookupPath) return std::string();
    Words path = split(env.variable("PATH"), ";");
    for (size_t d = 0; d < path.size(); ++d) {
        std::string dir = replaceAll(path[d], "\"", "");
        if (dir.empty() || dir == ".") continue;
        for (size_t n = 0; n < names.size(); ++n) {
            std::string full = combine(dir, names[n]);
            if (fs.isFile(full)) return full;
        }
    }
    if (looked) looked->push_back("PATH");
    return std::string();
}

// a library's folder: its environment variable, then its directory beside GRIDE.exe and under the
// installation, when the marker (or the folder) is there
std::string Context::findLibraryDir(const LibraryDef &lib) const {
    if (lib.directory.empty()) return std::string();
    if (!lib.directoryEnv.empty()) {
        std::string named = env.variable(lib.directoryEnv);
        if (!named.empty() && fs.isDirectory(named) && (lib.marker.empty() || fs.isFile(combine(named, lib.marker)))) return tidy(named);
    }
    Words dirs = searchDirs();
    for (size_t i = 0; i < dirs.size(); ++i) {
        std::string dir = combine(dirs[i], lib.directory);
        if (fs.isDirectory(dir) && (lib.marker.empty() || fs.isFile(combine(dir, lib.marker)))) return dir;
    }
    return std::string();
}

}}
