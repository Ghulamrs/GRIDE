#include "settings.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "about.h"
#include "json.h"
#include "path.h"
#include "product.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace editor {
namespace settings {

namespace {

// **A settings file is replaced whole or not at all**: written beside itself and renamed over, so a
// crash or a full disk mid-write leaves the old file and never an empty one. Windows's rename will
// not replace a file, so MoveFileEx does it there.
bool replaceFile(const std::string& file, const std::string& text) {
    std::string temporary = file + ".tmp";
    FILE* out = std::fopen(temporary.c_str(), "wb");
    if (!out) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), out) == text.size();
    if (std::fclose(out) != 0) ok = false;
    if (ok) {
#ifdef _WIN32
        int n = MultiByteToWideChar(CP_UTF8, 0, temporary.c_str(), -1, 0, 0);
        int m = MultiByteToWideChar(CP_UTF8, 0, file.c_str(), -1, 0, 0);
        std::wstring from(n > 0 ? n : 1, L'\0'), to(m > 0 ? m : 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, temporary.c_str(), -1, &from[0], n);
        MultiByteToWideChar(CP_UTF8, 0, file.c_str(), -1, &to[0], m);
        ok = MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = std::rename(temporary.c_str(), file.c_str()) == 0;
#endif
    }
    if (!ok) std::remove(temporary.c_str());
    return ok;
}

// Whether a file holds text that is not a JSON object - one a hand edit broke, say. Writing over it
// would lose every setting in it, so the write is refused and the file left for its owner to mend.
bool unreadable(const std::string& file) {
    FILE* in = std::fopen(file.c_str(), "rb");
    if (!in) return false;
    std::string text;
    char chunk[1024];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, in)) > 0) text.append(chunk, got);
    std::fclose(in);
    bool anything = false;
    for (size_t i = 0; i < text.size() && !anything; ++i)
        if (!std::isspace(static_cast<unsigned char>(text[i]))) anything = true;
    if (!anything) return false;
    std::string why;
    Json root = Json::parse(text, why);
    return !why.empty() || !root.is(Json::Object);
}

}

// The per-user state, ~/.gride/state.json: what was opened last and the choices
// made in the window. The configuration is settings.json, beside it.
std::string fileName() {
    std::string home = path::homeDir();
    if (home.empty()) return std::string();
    return path::join(path::join(home, product::kStateDirectory), product::kStateFile);
}

namespace {

Json readInstall();
bool writeInstall(const Json& root);

std::string toRead() {
    std::string now = fileName();
    if (!now.empty() && path::exists(now)) return now;
    return std::string();
}

}

namespace {

std::string* moved = 0;
bool movedTo() { return moved != 0; }
void rememberMoved(const std::string& where) { moved = new std::string(where); }

bool writeAll(const Json& root);

Json readAll() {
    std::string where = toRead();
    if (where.empty()) return Json::object();

    FILE* in = std::fopen(where.c_str(), "rb");
    if (!in) return Json::object();
    std::string text;
    char chunk[1024];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, in)) > 0) text.append(chunk, got);
    std::fclose(in);

    std::string why;
    Json root = Json::parse(text, why);
    if (why.empty() && root.is(Json::Object)) return root;

    bool anything = false;
    for (size_t i = 0; i < text.size(); ++i)
        if (!std::isspace(static_cast<unsigned char>(text[i]))) { anything = true; break; }

    if (anything && !movedTo()) {
        std::string aside = where + ".error";
        path::remove(aside);
        if (path::rename(where, aside)) {
            rememberMoved(aside);

            writeAll(Json::object());
        }
    }
    return Json::object();
}

bool writeAll(const Json& root) {
    std::string where = fileName();
    if (where.empty()) return false;

    path::makeDirectories(path::parent(where));

    return replaceFile(where, root.document());
}

}

bool plainFrame() { return readAll().get("plain").boolean(false); }

bool rememberPlainFrame(bool plain) {
    Json root = readAll();
    root.set("plain", Json::fromBool(plain));
    return writeAll(root);
}

std::string setAside() {
    readAll();
    return moved ? *moved : std::string();
}

std::string configuration() {
    std::string said = readAll().get("config").text("debug");
    return said == "release" ? said : std::string("debug");
}

bool rememberConfiguration(const std::string& which) {
    Json root = readAll();
    root.set("config", Json::fromText(which == "release" ? "release" : "debug"));
    return writeAll(root);
}

std::string codeFont() {
    std::string said = readInstall().get("font").text(std::string());
    // Carried forward from the per-user file, where it lived before.
    return said.empty() ? readAll().get("font").text(std::string()) : said;
}

bool rememberCodeFont(const std::string& described) {
    Json root = readInstall();
    root.set("font", Json::fromText(described));
    return writeInstall(root);
}

size_t indentWidth() {
    long width = readInstall().get("indent").integer(4);
    return (width < 1 || width > 16) ? 4 : static_cast<size_t>(width);
}

bool indentTabs() { return readInstall().get("tabs").boolean(false); }

bool rememberIndent(size_t width, bool tabs) {
    Json root = readInstall();
    root.set("indent", Json::fromNumber(static_cast<double>(width)));
    root.set("tabs", Json::fromBool(tabs));
    return writeInstall(root);
}

void carryFormerOver();

namespace {

// A pointer and never a std::string: in the C++/CLI window a native global with a destructor corrupts the onexit table before main (STATUS_HEAP_CORRUPTION under register_onexit_function); `moved` above is a pointer for the same reason.
std::string* pretended = 0;

// **settings.json is the user's**: on Windows GRIDE's one file in %APPDATA%\GRIDE, which every outlook
// shares (GRIDEx3); elsewhere in ~/.gride beside state.json. Program Files and /opt are not the user's
// to write, and the one there could not be changed. The installation's own file is the defaults the
// user's starts from; what either names is still resolved against the installation.
bool perUserInstallFile() {
    return !(pretended && !pretended->empty()) && !path::homeDir().empty();
}

std::string installDir() {
    if (pretended && !pretended->empty()) return *pretended;
    std::string where = path::programDirectory();
    if (where.empty()) return std::string();
    // GRIDE 1.0: the programs, settings.json, include\, lib\ and docs\ are beside GRIDE.exe (x64\<config>, or the
    // outlook's folder an installer made), so that folder is the installation; GRIDE's own bin\ under one still is.
    std::string leaf = path::filename(where);
    for (size_t i = 0; i < leaf.size(); ++i) leaf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(leaf[i])));
    return leaf == "bin" ? path::parent(where) : where;
}

Json readJsonFile(const std::string& file) {
    if (file.empty() || !path::exists(file)) return Json::object();

    FILE* in = std::fopen(file.c_str(), "rb");
    if (!in) return Json::object();
    std::string text;
    char chunk[1024];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, in)) > 0) text.append(chunk, got);
    std::fclose(in);

    std::string why;
    Json root = Json::parse(text, why);
    if (!why.empty() || !root.is(Json::Object)) return Json::object();
    return root;
}

// The installation's settings.json, which the per-user one is laid over: the installer's defaults.
std::string defaultsFile() {
    std::string base = installDir();
    return base.empty() ? std::string() : path::join(base, "settings.json");
}

// The defaults with the user's own over them, key by key: a key the user's file has wins, and one it
// lacks - one a later release added, say - comes from the installation.
Json readInstall() {
    carryFormerOver();
    Json mine = readJsonFile(installFile());
    if (!perUserInstallFile()) return mine;
    Json root = readJsonFile(defaultsFile());
    if (mine.is(Json::Object))
        for (size_t i = 0; i < mine.size(); ++i) root.set(mine.keyAt(i), mine.valueAt(i));
    return root;
}

// Written only where an installation is - the file already there, or
// include/ and lib/ beside it - so that a checkout built in place, or the
// suite driving its menus, never grows one.
bool writeInstall(const Json& root) {
    carryFormerOver();
    std::string file = installFile();
    if (file.empty()) return false;
    std::string base = installDir();
    if (perUserInstallFile()) path::makeDirectories(path::parent(file));
    else if (!path::exists(file) &&
        (!path::isDirectory(path::join(base, "include")) || !path::isDirectory(path::join(base, "lib"))))
        return false;
    // What was read of a file that would not parse was nothing, so writing now would keep one key.
    if (unreadable(file)) return false;
    return replaceFile(file, root.document());
}

// A directory named in the file, made absolute against it; else the
// directory of the key's name beside it, when that is there.
std::string installedDir(const char* key) {
    std::string base = installDir();
    if (base.empty()) return std::string();

    std::string said = readInstall().get(key).text(std::string());
    bool rooted = !said.empty() && (said[0] == '/' || said[0] == '\\' ||
                                    (said.size() > 1 && said[1] == ':'));
    std::string dir = said.empty() ? path::join(base, key)
                    : rooted     ? path::absolute(said)
                                 : path::absolute(path::join(base, said));
    return path::isDirectory(dir) ? dir : std::string();
}

// A program named in the file, made absolute against it the same way: the installer writes
// "bin/masm.exe" for the assembler, which is beside the editor wherever the installation was
// put; a full path is taken as written. What is not there counts for nothing, as before.
std::string installedFile(const char* key) {
    std::string said = readInstall().get(key).text(std::string());
    if (said.empty()) return std::string();
    bool rooted = said[0] == '/' || said[0] == '\\' || (said.size() > 1 && said[1] == ':');
    std::string base = installDir();
    std::string file = rooted || base.empty() ? said : path::absolute(path::join(base, said));
    return path::exists(file) ? file : std::string();
}

}

void pretendInstalledAt(const std::string& directory) {
    if (!pretended) pretended = new std::string();
    *pretended = directory;
}

#ifdef _WIN32
namespace {

// %APPDATA%\GRIDE - the folder of GRIDE's one settings.json, whichever outlook is installed (GRIDEx3).
std::string appDataGride() {
    const char* appData = std::getenv("APPDATA");
    if (appData && *appData) return path::join(appData, product::kName);
    return path::join(path::join(path::join(path::homeDir(), "AppData"), "Roaming"), product::kName);
}

// Where White kept the user's file before GRIDEx3: C:\Users\<you>\GRIDE 1.0\settings.json.
std::string formerInstallFile() {
    return path::join(path::join(path::homeDir(), std::string(product::kName) + " " + about::version()), "settings.json");
}

}
#endif

// **The file of the former place is carried over once** (GRIDEx3): each key it holds that settings.json
// lacks goes in - the keys Black and Grey keep there stay as they are - and then the former file goes,
// and its folder with it when nothing else is in it.
void carryFormerOver() {
#ifdef _WIN32
    if (!perUserInstallFile()) return;
    const std::string former = formerInstallFile(), now = installFile();
    if (!path::exists(former) || path::same(former, now)) return;
    Json old = readJsonFile(former);
    Json root = readJsonFile(now);
    for (size_t i = 0; i < old.size(); ++i)
        if (!root.has(old.keyAt(i))) root.set(old.keyAt(i), old.valueAt(i));
    path::makeDirectories(path::parent(now));
    if (unreadable(now) || !replaceFile(now, root.document())) return;
    path::remove(former);
    if (path::entries(path::parent(former)).empty()) path::removeTree(path::parent(former));
#endif
}

std::string installFile() {
#ifdef _WIN32
    // GRIDE's one settings.json, in %APPDATA%\GRIDE (GRIDEx3): White's keys beside Black's and Grey's,
    // whose names never meet White's; each outlook keeps the others' keys when it writes.
    if (perUserInstallFile())
        return path::join(appDataGride(), "settings.json");
#endif
    if (perUserInstallFile())
        return path::join(path::join(path::homeDir(), product::kStateDirectory), "settings.json");
    std::string base = installDir();
    return base.empty() ? std::string() : path::join(base, "settings.json");
}

std::string includeDir() { return installedDir("include"); }
std::string libDir() { return installedDir("lib"); }

std::string vcvars() {
    std::string said = readInstall().get("vcvars").text(std::string());
    return (!said.empty() && path::exists(said)) ? said : std::string();
}

// **Pointers, never std::string globals** - the trap `pretended` above names: linked into the
// C++/CLI window, a native global with a destructor corrupted the onexit table before main, and
// the window died on every start from 2026-09-18 until these three were found on the 19th.
static std::string* assemblerForThisRun = 0;
static std::string* linkerForThisRun = 0;
static std::string* tiForThisRun = 0;
static std::string* tilibForThisRun = 0;
static std::string* tilinkerForThisRun = 0;
static void overrideWith(std::string*& slot, const std::string& value) {
    if (!slot) slot = new std::string();
    *slot = value;
}
void overrideAssembler(const std::string& p) { overrideWith(assemblerForThisRun, p); }

// A plain bool, not a pointer: no destructor, so the window's rule holds.
static bool nativeForcedNow = false;
void forceNative(bool on) { nativeForcedNow = on; }
bool nativeForced() { return nativeForcedNow; }

bool askNative() {
    return readInstall().get("askNative").boolean(true);
}

bool rememberAskNative(bool ask) {
    Json root = readInstall();
    root.set("askNative", Json::fromBool(ask));
    return writeInstall(root);
}

// The compiler options a build uses when no project is open (options.h), kept as the dialog wrote them.
Json compilerOptions() { return readInstall().get("options"); }
bool rememberCompilerOptions(const Json& options) {
    Json root = readInstall();
    root.set("options", options);
    return writeInstall(root);
}

std::string assembler() {
    if (nativeForcedNow) return std::string();
    if (assemblerForThisRun && !assemblerForThisRun->empty()) return *assemblerForThisRun;
    return installedFile("assembler");
}

void overrideLinker(const std::string& p) { overrideWith(linkerForThisRun, p); }

std::string linker() {
    if (nativeForcedNow) return std::string();
    if (linkerForThisRun && !linkerForThisRun->empty()) return *linkerForThisRun;
    return installedFile("linker");
}

void overrideTi(const std::string& d) { overrideWith(tiForThisRun, d); }

namespace {

// Numbers read out of "ti-cgt-c6000_8.2.2", so 8.10 counts as newer than 8.2.
std::vector<long> versionOf(const std::string& name) {
    std::vector<long> parts;
    size_t at = name.find_last_of('_');
    for (size_t i = at == std::string::npos ? 0 : at + 1; i < name.size();) {
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) { ++i; continue; }
        long n = 0;
        while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) n = n * 10 + (name[i++] - '0');
        parts.push_back(n);
    }
    return parts;
}

void compilersIn(const std::string& dir, std::vector<std::string>& into) {
    std::vector<path::Entry> list = path::entries(dir);
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].directory && list[i].name.compare(0, 12, "ti-cgt-c6000") == 0) into.push_back(path::join(dir, list[i].name));
}

}

// **CCS's C6000 compiler where settings.json names none**: the newest ti-cgt-c6000 with a bin/lnk6x
// under where TI's installer puts one - C:\ti on Windows, ~/ti, /opt/ti and /Applications/ti elsewhere.
std::string detectedTi() {
    static std::string* cached = 0;  // a pointer, never freed: no static of class type in a mixed-mode image
    if (cached) return *cached;
    std::string found;
    std::vector<std::string> roots;
#ifdef _WIN32
    const char* drive = std::getenv("SystemDrive");
    roots.push_back(std::string(drive && *drive ? drive : "C:") + "\\ti");
#else
    roots.push_back(path::join(path::homeDir(), "ti"));
    roots.push_back("/opt/ti");
    roots.push_back("/Applications/ti");
#endif
    std::vector<std::string> candidates;
    for (size_t r = 0; r < roots.size(); ++r) {
        compilersIn(roots[r], candidates);
        std::vector<path::Entry> list = path::entries(roots[r]);
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].directory && list[i].name.compare(0, 3, "ccs") == 0) {
                const std::string ccs = path::join(roots[r], list[i].name);
                compilersIn(path::join(path::join(ccs, "tools"), "compiler"), candidates);
                compilersIn(path::join(path::join(path::join(ccs, "ccs"), "tools"), "compiler"), candidates);
            }
    }
    for (size_t i = 0; i < candidates.size(); ++i) {
        const std::string bin = path::join(candidates[i], "bin");
        if (!path::exists(path::join(bin, "lnk6x.exe")) && !path::exists(path::join(bin, "lnk6x"))) continue;
        if (found.empty() || versionOf(path::filename(candidates[i])) > versionOf(path::filename(found))) found = candidates[i];
    }
    cached = new std::string(found);
    return found;
}

std::string namedTi() {
    if (tiForThisRun && !tiForThisRun->empty()) return *tiForThisRun;
    std::string said = readInstall().get("ti").text(std::string());
    return (!said.empty() && path::isDirectory(said)) ? said : std::string();
}

std::string ti() {
    const std::string named = namedTi();
    return named.empty() ? detectedTi() : named;
}

void overrideTilib(const std::string& d) { overrideWith(tilibForThisRun, d); }

std::string tilib() {
    if (tilibForThisRun && !tilibForThisRun->empty()) return *tilibForThisRun;
    std::string said = readInstall().get("tilib").text(std::string());
    return (!said.empty() && path::isDirectory(said)) ? said : std::string();
}

void overrideTilinker(const std::string& p) { overrideWith(tilinkerForThisRun, p); }

std::string tilinker() {
    if (nativeForcedNow) return std::string();
    if (tilinkerForThisRun && !tilinkerForThisRun->empty()) return *tilinkerForThisRun;
    return installedFile("tilinker");
}

std::string namedTilinker() {
    if (tilinkerForThisRun && !tilinkerForThisRun->empty()) return *tilinkerForThisRun;
    if (nativeForcedNow) return std::string();
    return readInstall().get("tilinker").text(std::string());
}

namespace {

std::vector<std::string> installedList(const char* key) {
    std::vector<std::string> out;
    std::string base = installDir();
    // Held, not referenced off the temporary: a reference into
    // readInstall()'s result dangles once the statement ends.
    Json root = readInstall();
    const Json& list = root.get(key);
    for (size_t i = 0; i < list.size(); ++i) {
        std::string said = list.at(i).text("");
        if (said.empty()) continue;
        bool rooted = said[0] == '/' || said[0] == '\\' || (said.size() > 1 && said[1] == ':');
        out.push_back(rooted ? path::absolute(said) : path::absolute(path::join(base, said)));
    }
    return out;
}

bool rememberList(const char* key, const std::vector<std::string>& items) {
    Json root = readInstall();
    Json list = Json::array();
    for (size_t i = 0; i < items.size(); ++i) list.push(Json::fromText(items[i]));
    root.set(key, list);
    return writeInstall(root);
}

}

std::vector<std::string> includes() { return installedList("includes"); }
std::vector<std::string> libraries() { return installedList("libraries"); }
bool rememberIncludes(const std::vector<std::string>& dirs) { return rememberList("includes", dirs); }
bool rememberLibraries(const std::vector<std::string>& files) { return rememberList("libraries", files); }

namespace {

// A choice as settings.json or the environment spells it, lower case and trimmed; one not among `words`, auto.
std::string choiceOf(const std::string& said, const char* const* words, size_t count) {
    std::string word;
    for (size_t i = 0; i < said.size(); ++i)
        if (!std::isspace(static_cast<unsigned char>(said[i])))
            word += static_cast<char>(std::tolower(static_cast<unsigned char>(said[i])));
    for (size_t i = 0; i < count; ++i)
        if (word == words[i]) return word;
    return "auto";
}

const char* const kRuntimes[] = {"auto", "grt", "msvc"};
const char* const kDebuggers[] = {"auto", "gride-dbg", "cdb"};

}

std::string windowsRuntimeSetting() {
    return choiceOf(readInstall().get("windowsRuntime").text("auto"), kRuntimes, 3);
}

std::string windowsRuntime() {
    const char* given = std::getenv("GRIDE_WINDOWS_RUNTIME");
    if (given && *given) return choiceOf(given, kRuntimes, 3);
    return windowsRuntimeSetting();
}

bool rememberWindowsRuntime(const std::string& choice) {
    Json root = readInstall();
    root.set("windowsRuntime", Json::fromText(choiceOf(choice, kRuntimes, 3)));
    return writeInstall(root);
}

std::string windowsDebugger() {
    return choiceOf(readInstall().get("windowsDebugger").text("auto"), kDebuggers, 3);
}

bool rememberWindowsDebugger(const std::string& choice) {
    Json root = readInstall();
    root.set("windowsDebugger", Json::fromText(choiceOf(choice, kDebuggers, 3)));
    return writeInstall(root);
}

size_t buildJobs() {
    long jobs = readInstall().get("buildJobs").integer(0);
    return jobs < 0 ? 0 : static_cast<size_t>(jobs);
}

bool rememberBuildJobs(size_t jobs) {
    Json root = readInstall();
    root.set("buildJobs", Json::fromNumber(static_cast<double>(jobs)));
    return writeInstall(root);
}

std::string defaultCompiler() {
    std::string said = readInstall().get("compiler").text("auto");
    return said.empty() ? std::string("auto") : said;
}

bool rememberDefaultCompiler(const std::string& word) {
    Json root = readInstall();
    root.set("compiler", Json::fromText(word));
    return writeInstall(root);
}

bool rememberHeaderDirs(const std::string& include, const std::string& lib) {
    Json root = readInstall();
    root.set("include", Json::fromText(include));
    root.set("lib", Json::fromText(lib));
    return writeInstall(root);
}

bool rememberVcvars(const std::string& file) {
    Json root = readInstall();
    root.set("vcvars", Json::fromText(file));
    return writeInstall(root);
}

bool rememberAssembler(const std::string& file) {
    Json root = readInstall();
    root.set("assembler", Json::fromText(file));
    return writeInstall(root);
}

bool rememberLinker(const std::string& file) {
    Json root = readInstall();
    root.set("linker", Json::fromText(file));
    return writeInstall(root);
}

bool rememberTi(const std::string& dir, const std::string& lib) {
    Json root = readInstall();
    root.set("ti", Json::fromText(dir));
    root.set("tilib", Json::fromText(lib));
    return writeInstall(root);
}

bool rememberTilinker(const std::string& file) {
    Json root = readInstall();
    root.set("tilinker", Json::fromText(file));
    return writeInstall(root);
}

// A plain bool and a pointer, never a std::string global: the window's rule, as above.
static bool ccsForThisRun = false;
static std::string* ccsRootForThisRun = 0;
void overrideCcs(bool enabled, const std::string& root) { ccsForThisRun = enabled; overrideWith(ccsRootForThisRun, root); }

bool ccsEnabled() { return ccsForThisRun || readInstall().get("ccs").get("enabled").boolean(false); }

std::string ccsRoot() {
    if (ccsRootForThisRun && !ccsRootForThisRun->empty()) return *ccsRootForThisRun;
    Json root = readInstall();
    return root.get("ccs").get("root").text(std::string());
}

bool rememberCcs(bool enabled, const std::string& dir) {
    Json root = readInstall();
    Json ccs = root.get("ccs").is(Json::Object) ? root.get("ccs") : Json::object();
    ccs.set("enabled", Json::fromBool(enabled));
    ccs.set("root", Json::fromText(dir));
    root.set("ccs", ccs);
    return writeInstall(root);
}

Json ccsProjectState(const std::string& dir) {
    Json root = readInstall();
    return root.get("ccs").get("projects").get(path::withSlashes(path::absolute(dir)));
}

bool rememberCcsProjectState(const std::string& dir, const Json& state) {
    Json root = readInstall();
    Json ccs = root.get("ccs").is(Json::Object) ? root.get("ccs") : Json::object();
    Json projects = ccs.get("projects").is(Json::Object) ? ccs.get("projects") : Json::object();
    projects.set(path::withSlashes(path::absolute(dir)), state);
    ccs.set("projects", projects);
    root.set("ccs", ccs);
    return writeInstall(root);
}

#ifdef _WIN32
namespace {
// The newest earlier release's settings.json in the home directory - C:\Users\<you>\GRIDE 1.0 before
// GRIDE 1.0, say - so that a new release starts from what the user had chosen. Versions read as
// decimals, which is how this product's ran: 4.5, 4.51, 4.7, 5.0, 5.1, 5.12, 5.13, 5.14, 5.15, 5.16, 5.17. Empty when there is none.
std::string earlierReleaseFile() {
    std::string home = path::homeDir();
    if (home.empty()) return std::string();
    const double now = std::strtod(about::version(), 0);
    std::string best;
    double bestVersion = 0;
    // This major and the five before it; under each, "4.0" to "4.9" and "4.00" to "4.99".
    for (int major = static_cast<int>(now); major >= 1 && major >= static_cast<int>(now) - 5; --major)
        for (int minor = 0; minor < 110; ++minor) {
            char name[16];
            if (minor < 10) std::snprintf(name, sizeof name, "%d.%d", major, minor);
            else std::snprintf(name, sizeof name, "%d.%02d", major, minor - 10);
            const double v = std::strtod(name, 0);
            if (v >= now || v <= bestVersion) continue;
            std::string candidate =
                path::join(path::join(home, std::string(product::kName) + " " + name), "settings.json");
            if (path::exists(candidate)) { best = candidate; bestVersion = v; }
        }
    return best;
}
}
#endif

bool writeInstallFileIfAbsent() {
    std::string file = installFile();
    if (file.empty() || path::exists(file)) return true;
#ifdef _WIN32
    // A new release's file begins as the last release's choices over this installation's defaults.
    if (perUserInstallFile()) {
        std::string earlier = earlierReleaseFile();
        if (!earlier.empty()) {
            Json root = readJsonFile(defaultsFile());
            Json had = readJsonFile(earlier);
            for (size_t i = 0; i < had.size(); ++i) root.set(had.keyAt(i), had.valueAt(i));
            if (root.size() > 0) { writeInstall(root); return true; }
        }
    }
#endif
    // The installation's own, where there is one - its assembler and linkers are named there.
    if (perUserInstallFile() && path::exists(defaultsFile())) {
        Json given = readJsonFile(defaultsFile());
        if (given.is(Json::Object) && given.size() > 0) { writeInstall(given); return true; }
    }
    Json root = Json::object();
    root.set("include", Json::fromText("include"));
    root.set("lib", Json::fromText("lib"));
    root.set("vcvars", Json::fromText(""));
    root.set("assembler", Json::fromText(""));
    root.set("linker", Json::fromText(""));
    root.set("ti", Json::fromText(""));
    root.set("tilib", Json::fromText(""));
    root.set("tilinker", Json::fromText(""));
    root.set("askNative", Json::fromBool(true));
    root.set("windowsRuntime", Json::fromText("auto"));
    root.set("windowsDebugger", Json::fromText("auto"));
    root.set("buildJobs", Json::fromNumber(0));
    root.set("compiler", Json::fromText("auto"));
    root.set("indent", Json::fromNumber(4));
    root.set("tabs", Json::fromBool(false));
    root.set("font", Json::fromText(""));
    root.set("includes", Json::array());
    root.set("libraries", Json::array());
    Json ccs = Json::object();
    ccs.set("enabled", Json::fromBool(true));
    ccs.set("root", Json::fromText(""));
    root.set("ccs", ccs);
    writeInstall(root);   // declined where there is no installation, rightly
    return true;
}

namespace {

// A CCS project opened by one of its own files - .project, .cproject, .ccsproject - is the project
// in that folder: remembered as the folder, so it is one entry and not two, and has a name to show.
std::string projectEntry(const std::string& where) {
    std::string leaf = where;
    const size_t slash = leaf.find_last_of("/\\");
    if (slash != std::string::npos) leaf.erase(0, slash + 1);
    if ((leaf == ".project" || leaf == ".cproject" || leaf == ".ccsproject") && slash != std::string::npos)
        return where.substr(0, slash);
    return where;
}

}

std::vector<std::string> recentProjects() {
    std::vector<std::string> out;
    Json root = readAll();
    const Json& recent = root.get("recent");
    for (size_t i = 0; i < recent.size() && out.size() < 3; ++i) {
        std::string one = projectEntry(recent.at(i).text(""));
        if (one.empty() || !path::exists(one)) continue;
        bool seen = false;
        for (size_t k = 0; k < out.size(); ++k) seen = seen || path::oneName(out[k]) == path::oneName(one);
        if (!seen) out.push_back(one);
    }
    // The single "project" of earlier versions, carried in as the first.
    std::string project = root.get("project").text("");
    if (out.empty() && !project.empty() && path::exists(project)) out.push_back(project);
    return out;
}

std::vector<std::string> recentFiles() {
    std::vector<std::string> out;
    Json root = readAll();
    const Json& recent = root.get("recentFiles");
    for (size_t i = 0; i < recent.size() && out.size() < 3; ++i) {
        std::string one = recent.at(i).text("");
        if (!one.empty() && path::exists(one)) out.push_back(one);
    }
    return out;
}

bool rememberFile(const std::string& file) {
    if (fileName().empty() || file.empty()) return false;
    std::string now = path::absolute(file);
    std::vector<std::string> recent = recentFiles();
    Json list = Json::array();
    list.push(Json::fromText(now));
    for (size_t i = 0; i < recent.size() && list.size() < 3; ++i)
        if (path::oneName(recent[i]) != path::oneName(now)) list.push(Json::fromText(recent[i]));
    Json root = readAll();
    root.set("recentFiles", list);
    return writeAll(root);
}

std::string lastProject() {
    std::vector<std::string> recent = recentProjects();
    return recent.empty() ? std::string() : recent[0];
}

bool rememberProject(const std::string& directory) {
    if (fileName().empty() || directory.empty()) return false;

    std::string now = path::absolute(projectEntry(directory));
    std::vector<std::string> recent = recentProjects();
    Json list = Json::array();
    list.push(Json::fromText(now));
    for (size_t i = 0; i < recent.size() && list.size() < 3; ++i)
        if (path::oneName(recent[i]) != path::oneName(now)) list.push(Json::fromText(recent[i]));

    Json root = readAll();
    root.set("project", Json::fromText(now));
    root.set("recent", list);
    return writeAll(root);
}

}
}
