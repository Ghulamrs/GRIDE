// compile-tests.exe - the block's own tests. It takes the block's sources in directly (GRIDE_COMPILE_STATIC), so
// it can ask the parts with rules one by one and give the planner a machine that is not there.
//
//   compile-tests                                   the unit tests
//   compile-tests census <baseline\Black> <examples\projects> <tools folder> [-v]
//                                                   every recorded build of Black set against the block's plan
//   compile-tests patterns <examples\projects> <toolchain.json>  every example's name lists as patterns: the same files?
//   compile-tests rewrite <project.pro> <toolchain.json>         writes the .pro back with its name lists as patterns
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../gride_compile.h"
#include "../src/context.h"

using namespace gride::compile;

namespace {

int failures = 0, checks = 0;

void check(bool ok, const std::string &what) {
    ++checks;
    if (ok) return;
    ++failures;
    std::printf("FAIL  %s\n", what.c_str());
}

void equal(const std::string &got, const std::string &want, const std::string &what) {
    check(got == want, what + "\n        got:  " + got + "\n        want: " + want);
}

// ---- a machine that is not there ----
struct Fake : FileSystem, Environment {
    std::map<std::string, std::string> files;          // lower-cased tidy path -> bytes
    std::map<std::string, std::string> spelled;        // lower-cased tidy path -> the path as given
    std::map<std::string, std::string> vars;

    static std::string key(const std::string &p) { return lower(tidy(p)); }
    std::map<std::string, long long> times;            // every write is later than the one before
    long long clock;
    Fake() : clock(0) {}
    void put(const std::string &path, const std::string &bytes = "x") { files[key(path)] = bytes; spelled[key(path)] = tidy(path); times[key(path)] = ++clock; }

    bool isFile(const std::string &path) { return files.count(key(path)) != 0; }
    bool isDirectory(const std::string &path) {
        std::string f = key(path) + "\\";
        for (std::map<std::string, std::string>::iterator it = files.begin(); it != files.end(); ++it) if (startsWith(it->first, f)) return true;
        return false;
    }
    bool read(const std::string &path, std::string &bytes) {
        if (!isFile(path)) return false;
        bytes = files[key(path)];
        return true;
    }
    bool write(const std::string &path, const std::string &bytes) { put(path, bytes); return true; }
    long long modified(const std::string &path) { return isFile(path) ? times[key(path)] : 0; }
    std::vector<std::string> list(const std::string &folder) {
        std::vector<std::string> r;
        std::set<std::string> seen;
        std::string f = key(folder) + "\\";
        for (std::map<std::string, std::string>::iterator it = spelled.begin(); it != spelled.end(); ++it) {
            if (!startsWith(it->first, f)) continue;
            std::string rest = it->second.substr(f.size());
            size_t slash = rest.find('\\');
            std::string name = slash == std::string::npos ? rest : rest.substr(0, slash + 1);
            if (seen.insert(lower(name)).second) r.push_back(name);
        }
        return r;
    }
    std::string variable(const std::string &name) { return vars.count(name) ? vars[name] : std::string(); }
    std::string registry(const std::string &, const std::string &) { return std::string(); }
};

std::string words(const Words &w) { return join(w, "|"); }

// ---- the parts with rules ----

void testWords() {
    equal(words(cutWords("a  b\tc")), "a|b|c", "blanks part words");
    equal(words(cutWords("-S \"$(Source)\" -o \"a b\"")), "-S|$(Source)|-o|a b", "a quoted stretch is one word");
    equal(words(cutWords("/Fd\"$(ObjDir)\\vc.pdb\" x")), "/Fd$(ObjDir)\\vc.pdb|x", "quotes inside a word");
    equal(words(cutWords("\"C:\\dir\\\\\" next")), "C:\\dir\\|next", "two backslashes before a closing quote are one");
    equal(words(cutWords("say \\\"hi\\\"")), "say|\"hi\"", "an escaped quote is a quote");
    equal(words(cutWords("\"\" x")), "|x", "an empty quoted word is a word");

    Words argv;
    argv.push_back("plain"); argv.push_back("two words"); argv.push_back("C:\\dir with blank\\"); argv.push_back("say \"hi\""); argv.push_back("");
    std::string line = commandLine("C:\\Program Files\\x.exe", argv);
    Words back = cutWords(line);
    check(back.size() == 6 && back[0] == "C:\\Program Files\\x.exe", "a command line reads back: the program");
    for (size_t i = 0; i < argv.size() && i + 1 < back.size(); ++i) equal(back[i + 1], argv[i], "a command line reads back: argument " + std::to_string(i));
    equal(commandLine("a.exe", Words(1, "-Ix")), "a.exe -Ix", "nothing is quoted that needs no quote");
}

void testFill() {
    Table t;
    t["Source"] = Words(1, "C:\\my project\\main.c");
    t["Flags"] = cutWords("-g -D_DEBUG=1");
    t["Empty"] = Words();
    t["Objects"].push_back("a b.obj"); t["Objects"].push_back("c.obj");
    t["Out"] = Words(1, "C:\\out dir\\p.exe");
    Words unknown;
    Words r = fill("-S \"$(Source)\" $(Flags) $(Empty) /out:\"$(Out)\" $(Objects) $(Nobody)", t, &unknown);
    equal(words(r), "-S|C:\\my project\\main.c|-g|-D_DEBUG=1|/out:C:\\out dir\\p.exe|a b.obj|c.obj|$(Nobody)", "a template filled word by word");
    equal(words(unknown), "Nobody", "an unknown placeholder is reported");
    t["Dir"] = Words(1, "C:\\ends in slash\\");
    equal(words(fill("-i \"$(Dir)\" next", t)), "-i|C:\\ends in slash\\|next", "a value ending in a backslash stays one word");
}

void testPaths() {
    equal(tidy("C:/a/./b/../c/"), "C:\\a\\c", "tidy");
    equal(combine("C:\\p", "src/a.c"), "C:\\p\\src\\a.c", "combine");
    equal(combine("C:\\p", "D:\\x\\a.c"), "D:\\x\\a.c", "combine with an absolute path");
    equal(combine("C:\\p\\q", "..\\shared\\u.c"), "C:\\p\\shared\\u.c", "combine with ..");
    equal(relative("C:\\P", "c:\\p\\Src\\a.c"), "Src\\a.c", "relative, without regard to case");
    equal(relative("C:\\p", "C:\\px\\a.c"), "C:\\px\\a.c", "a folder whose name only begins the same is not below");
    equal(extension("a/b.C++"), ".c++", "extension");
    equal(stem("x\\main.test.c"), "main.test", "stem");
    check(matches("*.cpp", "Main.CPP") && !matches("*.cpp", "main.cpp.bak") && matches("m?in*.c", "main2.c"), "wildcards");
}

void testMerge() {
    std::string e;
    Json shipped = Json::parse("{\"tools\":{\"c90\":{\"exe\":\"c90.exe\",\"aliases\":[\"a\",\"b\"],\"lookup\":{\"env\":\"C90\",\"path\":true}}},\"format\":1}", e);
    Json user = Json::parse("{\"tools\":{\"c90\":{\"aliases\":[\"z\"],\"lookup\":{\"path\":false}},\"mine\":{\"exe\":\"m.exe\"}}}", e);
    Json all = merged(shipped, user);
    const Json &c90 = all.get("tools").get("c90");
    equal(c90.get("exe").text(), "c90.exe", "merge: a field the user does not name stays");
    check(c90.get("aliases").size() == 1 && c90.get("aliases").at(0).text() == "z", "merge: a list replaces the shipped list");
    check(c90.get("lookup").get("env").text() == "C90" && !c90.get("lookup").get("path").boolean(), "merge: objects merge field by field");
    equal(all.get("tools").get("mine").get("exe").text(), "m.exe", "merge: a new tool is added");
    check(all.get("format").integer() == 1, "merge: the rest is untouched");
}

void testSettings() {
    std::string e;
    Json s = Json::parse("{\"GrideIncludeDir\":\"gi\",\"WindowsRuntime\":\"MSVC\",\"environment\":{\"grideIncludeDir\":\"bi\",\"sharedIncludes\":[\"x\"]},"
                         "\"include\":\"wi\",\"DefaultCompiler\":\"cl\"}", e);
    equal(readBuildSettings(s, "Black").include, "bi", "settings: Black's old key first for Black");
    equal(readBuildSettings(s, "Grey").include, "gi", "settings: Grey's old key first for Grey");
    equal(readBuildSettings(s, "White").include, "wi", "settings: White's old key first for White");
    equal(readBuildSettings(s, "White").compiler, "cl", "settings: another outlook's key where its own is absent");
    equal(readBuildSettings(s, "Black").windowsRuntime, "msvc", "settings: the runtime word in lower case");
    Json b = Json::parse("{\"build\":{\"include\":\"new\"},\"GrideIncludeDir\":\"gi\"}", e);
    equal(readBuildSettings(b, "Grey").include, "new", "settings: build.<key> wins over every old key");
}

extern const char *toolchainForTests;

Definitions definitionsOf(const std::string &text) {
    std::string error;
    Words wrong;
    return readDefinitions(Json::parse(text, error), wrong);
}

Definitions definitionsIn(const std::string &file) {
    std::string text;
    systemFileSystem().read(file, text);
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    return definitionsOf(text);
}

Project projectOf(Fake &fs, const std::string &text) {
    fs.put("C:\\p\\demo.pro", text);
    Project p;
    std::string why;
    check(readProject("C:\\p\\demo.pro", fs, p, why), "the project reads: " + why);
    return p;
}

std::string filesOf(const ProjectFiles &f, const std::string &group) {
    Words r;
    for (size_t i = 0; i < f.files.size(); ++i) if (f.files[i].group == group) r.push_back(f.files[i].relative);
    return join(r, " ");
}

void testPatterns() {
    Fake fs;
    const char *there[] = { "main.cpp", "Zeta.cpp", "alpha.cpp", "sample.cpp", "notes.txt", "a.h", "B.hpp", "backend\\gen.cpp", "backend\\x86\\deep.cpp",
                            "backend\\emit.cpp", "obj\\Debug\\Win\\main.s", "scratch1.cpp", "demo.build.log" };
    for (size_t i = 0; i < sizeof there / sizeof *there; ++i) fs.put(std::string("C:\\p\\") + there[i]);
    Project p = projectOf(fs, "{\"groups\":{\"Sources\":[\"*.cpp\",\"backend/*.cpp\"],\"Headers\":[\"*.h\",\"*.hpp\"],\"Input\":[\"sample.cpp\"],"
                              "\"Asm\":[\"obj/Debug/Win/*.s\"],\"Logs\":[\"*.log\"],\"None\":[\"*.xyz\"]},\"exclude\":[\"scratch*.cpp\"]}");
    Words never(1, "C:\\p\\obj\\Debug\\Win");
    ProjectFiles f = resolveFiles(p, fs, never);
    equal(filesOf(f, "Sources"), "alpha.cpp main.cpp Zeta.cpp backend/emit.cpp backend/gen.cpp",
          "patterns: sorted by name without regard to case; a folder is not searched below itself; a name beats a pattern; exclude");
    equal(filesOf(f, "Headers"), "a.h B.hpp", "patterns: two entries of one group, in the order written");
    equal(filesOf(f, "Input"), "sample.cpp", "patterns: the named file is its group's");
    equal(filesOf(f, "Asm"), "", "patterns: nothing is taken from the object folder");
    equal(filesOf(f, "Logs"), "", "patterns: the build log is never a file of the project");
    bool noneSaid = false;
    for (size_t i = 0; i < f.listed.size(); ++i) if (f.listed[i].entry == "*.xyz" && f.listed[i].pattern && f.listed[i].files.empty()) noneSaid = true;
    check(noneSaid, "patterns: a pattern that matches nothing is listed with no file");

    Project q = projectOf(fs, "{\"groups\":{\"A\":[\"*.cpp\"],\"B\":[\"*.cpp\",\"main.cpp\"]}}");
    ProjectFiles g = resolveFiles(q, fs, Words());
    check(filesOf(g, "B") == "main.cpp" && filesOf(g, "A").find("main.cpp") == std::string::npos, "patterns: a name in a later group beats a pattern in an earlier one");
    check(filesOf(g, "A").find("alpha.cpp") != std::string::npos, "patterns: between two patterns the first group wins");

    Project m = projectOf(fs, "{\"groups\":{\"S\":[\"gone.c\",\"main.cpp\"]}}");
    ProjectFiles h = resolveFiles(m, fs, Words());
    check(h.files.size() == 2 && h.files[0].missing && !h.files[1].missing, "patterns: a named file that is gone is listed as missing");

    // name lists written as patterns
    Fake fs2;
    const char *there2[] = { "a.cpp", "b.cpp", "sample.cpp", "a.h", "lib\\x.c", "lib\\y.c", "lib\\skip.c" };
    for (size_t i = 0; i < sizeof there2 / sizeof *there2; ++i) fs2.put(std::string("C:\\p\\") + there2[i]);
    Project n = projectOf(fs2, "{\"groups\":{\"Sources\":[\"a.cpp\",\"b.cpp\"],\"Headers\":[\"a.h\"],\"Input\":[\"sample.cpp\"],\"Lib\":[\"lib/x.c\",\"lib/y.c\"]}}");
    Definitions defs = definitionsOf(toolchainForTests);
    std::vector<Group> c = compacted(n, fs2, Words(), defs);
    equal(words(c[0].entries), "*.cpp", "compacting: the group that names the most takes the pattern");
    equal(words(c[1].entries), "*.h", "compacting: one header, one pattern");
    equal(words(c[2].entries), "sample.cpp", "compacting: the other group keeps its name");
    equal(words(c[3].entries), "lib/x.c|lib/y.c", "compacting: a folder with a file nobody names keeps its names");
    Project n2 = n;
    n2.groups = c;
    ProjectFiles before = resolveFiles(n, fs2, Words()), after = resolveFiles(n2, fs2, Words());
    for (size_t i = 0; i < n.groups.size(); ++i)
        equal(filesOf(after, n.groups[i].name), filesOf(before, n.groups[i].name), "compacting: " + n.groups[i].name + " gives the files it gave");

    // a language compiled as one run: its first file is the program's own and must stay first
    Fake fs3;
    const char *there3[] = { "primes.u", "numbers.u", "zeta.u" };
    for (size_t i = 0; i < sizeof there3 / sizeof *there3; ++i) fs3.put(std::string("C:\\p\\") + there3[i]);
    Project u = projectOf(fs3, "{\"groups\":{\"Sources\":[\"primes.u\",\"zeta.u\",\"numbers.u\"]}}");
    std::vector<Group> cu = compacted(u, fs3, Words(), defs);
    equal(words(cu[0].entries), "primes.u|*.u", "compacting: the first file of a language compiled as one keeps its name in front of the pattern");
    Project u2 = u;
    u2.groups = cu;
    equal(filesOf(resolveFiles(u2, fs3, Words()), "Sources"), "primes.u numbers.u zeta.u", "compacting: so it stays first, and the others follow by name");
    Project v = projectOf(fs3, "{\"groups\":{\"Sources\":[\"numbers.u\",\"primes.u\",\"zeta.u\"]}}");
    equal(words(compacted(v, fs3, Words(), defs)[0].entries), "*.u", "compacting: a first file that the pattern gives first anyway needs no name");
}

const char *toolchainForTests =
    "{\"languages\":{\"C\":{\"role\":\"cCompiler\",\"extensions\":[\".c\"]},\"C++\":{\"role\":\"cppCompiler\",\"extensions\":[\".cpp\"]},"
    "\"Assembly\":{\"role\":\"assembler\",\"extensions\":[\".s\"]},\"Unit\":{\"role\":\"unitCompiler\",\"extensions\":[\".u\"]}},\"headerExtensions\":[\".h\"],"
    "\"libraries\":{\"urt\":{\"kind\":\"unitRuntime\",\"target\":\"box\",\"directory\":\"lib\",\"marker\":\"urt.lib\",\"files\":{\"Debug\":\"urtd.lib\",\"Release\":\"urt.lib\"},\"when\":\"unit\"}},"
    "\"folders\":{\"output\":\"out/$(Config)/$(TargetFolder)\",\"objects\":\"obj/$(Config)/$(TargetFolder)\"},"
    "\"compilerOptions\":{\"options\":{"
    "\"cc.opt\":{\"control\":\"choice\",\"default\":{\"Debug\":\"-O0\",\"Release\":\"-O2\"},\"flag\":\"$(Value)\",\"silent\":\"-O0\"},"
    "\"cc.g\":{\"control\":\"check\",\"default\":{\"Debug\":\"1\",\"Release\":\"0\"},\"flagOn\":\"-g\",\"needs\":\"lineTable\"},"
    "\"cc.defines\":{\"control\":\"text\",\"default\":{\"Debug\":\"_DEBUG=1\",\"Release\":\"NDEBUG=1\"},\"flagEach\":\"-D$(Item)\"},"
    "\"cc.tell\":{\"control\":\"check\",\"default\":\"0\",\"environment\":\"CC_TELL\"}}},"
    "\"targets\":{\"box\":{\"ideTarget\":\"Box\",\"folder\":\"Box\",\"host\":true,\"debugInfo\":\"dwarf\",\"assemblyExtension\":\".s\",\"objectExtension\":\".o\","
    "\"programExtension\":\".exe\",\"libraryDirFlag\":\"-L\",\"libraryFlag\":\"-l \","
    "\"roles\":{\"cCompiler\":\"cc\",\"cppCompiler\":\"cxx\",\"unitCompiler\":\"uc\",\"assembler\":\"as\",\"linker\":\"ld\"}},"
    "\"far\":{\"ideTarget\":\"Far\",\"folder\":\"Far\",\"host\":false,\"assemblyExtension\":\".s\",\"objectExtension\":\".o\","
    "\"roles\":{\"cCompiler\":\"cc\",\"assembler\":\"as\",\"linker\":\"ld\"}}},"
    "\"tools\":{"
    "\"cc\":{\"role\":\"cCompiler\",\"languages\":[\"C\"],\"exe\":\"cc.exe\",\"words\":[\"cc\"],\"lookup\":{\"installDir\":\"bin\"},\"archFlag\":\"-arch $(Arch)\","
    "\"includeFlag\":\"-I\",\"defineFlag\":\"-D\",\"options\":[\"cc.opt\",\"cc.g\",\"cc.defines\",\"cc.tell\"],"
    "\"operations\":{\"compileToAssembly\":{\"args\":\"-S \\\"$(Source)\\\" -o \\\"$(AsmFile)\\\" $(ArchFlag) $(ConfigFlags) $(IncludeFlags) $(DefineFlags) $(CompileFlags)\"}}},"
    "\"cxx\":{\"role\":\"cppCompiler\",\"languages\":[\"C++\"],\"exe\":\"cxx.exe\",\"words\":[\"cxx\"],\"lookup\":{\"installDir\":\"bin\"},"
    "\"configFlags\":{\"Debug\":\"-g\",\"Release\":\"-O2\"},"
    "\"operations\":{\"compileToAssembly\":{\"args\":\"-S \\\"$(Source)\\\" -o \\\"$(AsmFile)\\\" $(ConfigFlags)\"}}},"
    "\"uc\":{\"role\":\"unitCompiler\",\"languages\":[\"Unit\"],\"exe\":\"uc.exe\",\"lookup\":{\"installDir\":\"bin\"},"
    "\"operations\":{\"compileToAssembly\":{\"args\":\"-S $(Sources) -o \\\"$(AsmFile)\\\"\"}}},"
    "\"as\":{\"role\":\"assembler\",\"exe\":\"as.exe\",\"lookup\":{\"installDir\":\"bin\"},\"operations\":{\"assemble\":{\"args\":\"\\\"$(AsmFile)\\\" -o \\\"$(ObjFile)\\\"\"}}},"
    "\"ld\":{\"role\":\"linker\",\"exe\":\"ld.exe\",\"lookup\":{\"installDir\":\"bin\"},\"linkFlags\":{\"Debug\":\"--debug\"},\"responseFile\":\"@$(File)\","
    "\"operations\":{\"link\":{\"args\":\"$(Objects) -o \\\"$(Output)\\\" $(LibDirFlags) $(LibFlags) $(LinkFlags)\"}}}}}";

struct Bench {
    Fake fs;
    Paths paths;
    Bench() {
        paths.programFolder = "C:\\gride";
        paths.shippedToolchain = "C:\\gride\\toolchain.json";
        fs.put(paths.shippedToolchain, toolchainForTests);
        const char *tools[] = { "cc.exe", "cxx.exe", "as.exe", "ld.exe", "uc.exe", "lib\\urt.lib", "lib\\urtd.lib" };
        for (int i = 0; i < 7; ++i) fs.put(std::string("C:\\gride\\") + tools[i]);
    }
    Plan plan(const std::string &pro, const std::string &config = "Debug", const std::string &target = "box", Action action = Build, const std::string &file = "") {
        fs.put("C:\\p\\demo.pro", pro);
        Workspace w = Workspace::load(paths, fs, fs);
        Request r;
        r.project = "C:\\p\\demo.pro";
        r.configuration = config;
        r.target = target;
        r.action = action;
        r.file = file;
        return w.plan(r);
    }
};

std::string line(const Step &s) { return leaf(s.program) + " " + join(s.argv, " "); }
std::string problemsOf(const Plan &p) {
    Words r;
    for (size_t i = 0; i < p.problems.size(); ++i) r.push_back(p.problems[i].severity + ": " + p.problems[i].text);
    return join(r, " / ");
}

void testPlan() {
    Bench b;
    b.fs.put("C:\\p\\main.c"); b.fs.put("C:\\p\\util.c"); b.fs.put("C:\\p\\start.s"); b.fs.put("C:\\p\\a.h");
    Plan p = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"*.c\",\"start.s\"],\"Headers\":[\"*.h\"]},\"include\":[\"inc\"]}");
    check(p.runnable(), "plan: a plain project plans (" + problemsOf(p) + ")");
    check(p.steps.size() == 6, "plan: two compiles, three assembles, one link - " + std::to_string(p.steps.size()) + " steps");
    if (p.steps.size() == 6) {
        equal(line(p.steps[0]), "cc.exe -S C:\\p\\main.c -o C:\\p\\obj\\Debug\\Box\\main.s -arch box -g -D_DEBUG=1 -IC:\\p\\inc", "plan: the compile of main.c");
        equal(line(p.steps[2]), "as.exe C:\\p\\obj\\Debug\\Box\\main.s -o C:\\p\\obj\\Debug\\Box\\main.o", "plan: its assembly");
        equal(line(p.steps[4]), "as.exe C:\\p\\start.s -o C:\\p\\obj\\Debug\\Box\\start.o", "plan: an assembly source is assembled where it is");
        equal(line(p.steps[5]), "ld.exe C:\\p\\obj\\Debug\\Box\\main.o C:\\p\\obj\\Debug\\Box\\util.o C:\\p\\obj\\Debug\\Box\\start.o -o C:\\p\\out\\Debug\\Box\\demo.exe --debug",
              "plan: the link, objects in the files' order, the linker's Debug flag from toolchain.json");
        check(p.steps[0].wave == 0 && p.steps[2].wave == 1 && p.steps[5].wave == 2, "plan: compiles, then assembles, then the link");
        check(p.steps[0].inputs.size() == 2 && p.steps[0].inputs[1] == "C:\\p\\a.h", "plan: the project's headers are inputs of every compile");
        check(p.steps[0].why.find("by extension (.c is C") != std::string::npos, "plan: the step says why its compiler was chosen: " + p.steps[0].why);
        check(!p.steps[0].stamp.empty() && p.steps[0].stamp != p.steps[1].stamp, "plan: every step has its own stamp");
    }
    Plan rel = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"main.c\"]}}", "Release");
    if (!rel.steps.empty()) equal(line(rel.steps[0]), "cc.exe -S C:\\p\\main.c -o C:\\p\\obj\\Release\\Box\\main.s -arch box -O2 -DNDEBUG=1", "plan: Release takes the options' Release defaults");
    check(rel.steps.size() == 3 && line(rel.steps[2]).find("--debug") == std::string::npos, "plan: no Debug link flag in Release");

    Plan own = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"main.c\"]},\"options\":{\"debug\":{\"cc.opt\":\"-O1\",\"cc.g\":\"0\",\"cc.defines\":\"A;B=2\",\"cc.tell\":\"1\"}}}");
    if (!own.steps.empty()) {
        equal(line(own.steps[0]), "cc.exe -S C:\\p\\main.c -o C:\\p\\obj\\Debug\\Box\\main.s -arch box -O1 -DA -DB=2", "plan: the project's own Compiler Options");
        check(own.steps[0].environment.size() == 1 && own.steps[0].environment[0].first == "CC_TELL" && own.steps[0].environment[0].second == "1",
              "plan: an option that sets a variable sets it on its step only");
        check(own.steps[0].stamp != p.steps[0].stamp, "plan: a changed option changes the stamp");
    }
    check(p.steps[0].environment.empty(), "plan: an option that is off sets no variable");

    Plan cfg = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"main.c\"]},\"configurations\":{\"Debug|Box\":{\"outDir\":\"bin\",\"objDir\":\"tmp/o\","
                      "\"includeDirs\":[\"$(ProjectDir)hdr\"],\"defines\":[\"MINE\"],\"libs\":[\"m\"],\"libDirs\":[\"L\"],\"compileFlags\":\"-x \\\"a b\\\"\",\"outputName\":\"prog\"}}}");
    if (cfg.steps.size() == 3) {
        equal(line(cfg.steps[0]), "cc.exe -S C:\\p\\main.c -o C:\\p\\tmp\\o\\main.s -arch box -g -D_DEBUG=1 -IC:\\p\\hdr -DMINE -x a b", "plan: a stored configuration");
        equal(line(cfg.steps[2]), "ld.exe C:\\p\\tmp\\o\\main.o -o C:\\p\\bin\\prog.exe -LC:\\p\\L -l m --debug", "plan: its folders, name and libraries in the link");
    } else check(false, "plan: a stored configuration plans - " + problemsOf(cfg));

    // Grey's form of the configurations: a list, an entry by its name
    std::string greys = "{\"name\":\"demo\",\"groups\":{\"Sources\":[\"main.c\"]},\"buildConfigurations\":["
                        "{\"name\":\"Debug\",\"defines\":[\"MINE\"],\"flags\":\"-w\",\"linkFlags\":\"--map\",\"outputDir\":\"build\\\\$(Target)\\\\$(Config)\"},"
                        "{\"name\":\"Release-small\",\"defines\":[],\"flags\":\"\",\"outputDir\":\"dist\\\\$(Config)\"}]}";
    Plan grey = b.plan(greys);
    if (grey.steps.size() == 3) {
        equal(line(grey.steps[0]), "cc.exe -S C:\\p\\main.c -o C:\\p\\obj\\Debug\\Box\\main.s -arch box -g -D_DEBUG=1 -DMINE -w",
              "plan: Grey's configuration list - its defines and flags; the folder Grey gave every project is not the project's choice");
        equal(line(grey.steps[2]), "ld.exe C:\\p\\obj\\Debug\\Box\\main.o -o C:\\p\\out\\Debug\\Box\\demo.exe --map --debug", "plan: and its link flags");
    } else check(false, "plan: Grey's configuration list plans - " + problemsOf(grey));
    Plan small = b.plan(greys, "Release-small");
    if (small.steps.size() == 3) {
        equal(line(small.steps[0]), "cc.exe -S C:\\p\\main.c -o C:\\p\\dist\\Release-small\\obj\\main.s -arch box -O2 -DNDEBUG=1",
              "plan: a configuration by its own name is a Release by its kind, in the folder it names, objects in obj under it");
        check(small.program == "C:\\p\\dist\\Release-small\\demo.exe", "plan: and its program there: " + small.program);
    } else check(false, "plan: a named configuration plans - " + problemsOf(small));
    Plan named2 = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"main.c\"]}}", "Release-small");
    check(named2.program == "C:\\p\\out\\Release-small\\Box\\demo.exe", "plan: a configuration the project does not store goes by the rule, under its name: " + named2.program);

    b.fs.put("C:\\p\\src\\a\\util.c"); b.fs.put("C:\\p\\src\\b\\util.c");
    Plan two = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"src/a/util.c\",\"src/b/util.c\",\"main.c\"]}}");
    check(two.steps.size() == 7 && line(two.steps[0]).find("src_a_util.s") != std::string::npos && line(two.steps[1]).find("src_b_util.s") != std::string::npos &&
          line(two.steps[2]).find("\\main.s") != std::string::npos, "plan: two sources of one name get objects named from their paths");

    b.fs.put("C:\\p\\x.cpp");
    Plan named = b.plan("{\"name\":\"demo\",\"toolchain\":\"cc\",\"groups\":{\"Sources\":[\"main.c\",\"x.cpp\"]}}");
    check(!named.runnable() && problemsOf(named).find("does not compile C++") != std::string::npos, "plan: a named compiler that does not take a file's language is refused: " + problemsOf(named));
    Plan group = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"main.c\"],\"Cxx\":{\"files\":[\"x.cpp\"],\"toolchain\":\"cxx\"}}}");
    check(group.runnable() && group.steps.size() == 5 && group.steps[1].tool == "cxx" && group.steps[1].why.find("the group Cxx names cxx") != std::string::npos,
          "plan: a group's own compiler word, and the step says so");
    if (group.steps.size() == 5) equal(line(group.steps[1]), "cxx.exe -S C:\\p\\x.cpp -o C:\\p\\obj\\Debug\\Box\\x.s -g", "plan: a compiler with no options takes its configFlags");

    Plan gone = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"main.c\",\"lost.c\"]}}");
    check(!gone.runnable() && problemsOf(gone).find("lost.c") != std::string::npos, "plan: a named source that is gone is an error");
    Plan none = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"*.zzz\"]}}");
    check(!none.runnable() && problemsOf(none).find("no source files") != std::string::npos, "plan: a build with no source is an error");
    Plan other = b.plan("{\"name\":\"demo\",\"arch\":\"far\",\"groups\":{\"Sources\":[\"main.c\"]}}");
    check(other.steps.empty() && other.runnable() && problemsOf(other).find("builds for far only") != std::string::npos, "plan: a project that names its arch builds for no other: " + problemsOf(other));
    Plan cross = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"main.c\"]}}", "Debug", "far");
    check(cross.steps.size() == 1 && cross.steps[0].kind == "compile" && cross.program.empty(), "plan: another machine's target is compiled to assembly and no further");
    check(cross.steps.size() == 1 && line(cross.steps[0]).find(" -g") == std::string::npos, "plan: no -g where the target carries no line table");
    Plan build = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"main.c\"],\"Extra\":[\"util.c\"]},\"build\":{\"target\":\"tool\",\"groups\":[\"Sources\"]}}");
    check(build.steps.size() == 3 && build.program == "C:\\p\\out\\Debug\\Box\\tool.exe", "plan: only the build's groups are built, and the program takes the build's name");
    Plan excl = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"*.c\"]},\"excludeFromBuild\":[\"util.c\"]}");
    check(excl.steps.size() == 3, "plan: a file excluded from the build is not compiled");

    // a language compiled as one run: every file in one compile, named after the program; its library first in the link
    b.fs.put("C:\\p\\primes.u"); b.fs.put("C:\\p\\numbers.u");
    Plan unit = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"primes.u\",\"numbers.u\"]},\"build\":{\"target\":\"primes\"}}");
    check(unit.runnable() && unit.steps.size() == 3, "plan: a program compiled as one run plans - " + problemsOf(unit));
    if (unit.steps.size() == 3) {
        equal(line(unit.steps[0]), "uc.exe -S C:\\p\\primes.u C:\\p\\numbers.u -o C:\\p\\obj\\Debug\\Box\\primes.s", "plan: every file of it in one compile, in the order written");
        equal(line(unit.steps[2]), "ld.exe C:\\p\\obj\\Debug\\Box\\primes.o -o C:\\p\\out\\Debug\\Box\\primes.exe -l C:\\gride\\lib\\urtd.lib --debug",
              "plan: the library toolchain.json gives that language, the Debug one, in the link");
    }
    Plan unitByPattern = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"primes.u\",\"*.u\"]},\"build\":{\"target\":\"primes\"}}");
    if (!unitByPattern.steps.empty())
        equal(line(unitByPattern.steps[0]), "uc.exe -S C:\\p\\primes.u C:\\p\\numbers.u -o C:\\p\\obj\\Debug\\Box\\primes.s", "plan: the same from a name and a pattern");

    Plan clean = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"main.c\"]}}", "Debug", "box", Clean);
    check(clean.steps.empty() && clean.cleans.size() == 2, "plan: Clean names the two folders and runs nothing");
    Plan bad = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"main.c\"]},\"configurations\":{\"Debug|Box\":{\"outDir\":\".\"}}}", "Debug", "box", Clean);
    check(!bad.runnable(), "plan: Clean refuses a folder that is the project's own");
    Plan onefile = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"main.c\",\"util.c\"]}}", "Debug", "box", CompileFile, "C:\\p\\util.c");
    check(onefile.steps.size() == 2 && line(onefile.steps[0]).find("util.c") != std::string::npos, "plan: Compile File compiles and assembles the one file");

    std::string many = "{\"name\":\"demo\",\"groups\":{\"Sources\":[";
    for (int i = 0; i < 900; ++i) {
        std::string name = "a_rather_long_source_file_name_number_" + std::to_string(i) + ".c";
        b.fs.put("C:\\p\\" + name);
        many += (i ? ",\"" : "\"") + name + "\"";
    }
    Plan big = b.plan(many + "]}}");
    check(big.runnable() && !big.steps.empty() && big.steps.back().argv.size() == 1 && startsWith(big.steps.back().argv[0], "@C:\\p\\obj\\Debug\\Box\\"),
          "plan: a link line Windows would refuse goes into a file of arguments");
    check(big.files.size() == 1, "plan: and the file is among the plan's files to write");

    b.fs.files.erase(Fake::key("C:\\gride\\as.exe"));
    Plan lost = b.plan("{\"name\":\"demo\",\"groups\":{\"Sources\":[\"main.c\"]}}");
    check(!lost.runnable() && problemsOf(lost).find("as not found (looked in beside GRIDE.exe") != std::string::npos, "plan: a tool that is nowhere is an error naming the places: " + problemsOf(lost));
}

void testLog() {
    Bench b;
    b.fs.put("C:\\p\\main.c"); b.fs.put("C:\\p\\util.c");
    b.fs.put("C:\\p\\demo.pro", "{\"name\":\"demo\",\"groups\":{\"Sources\":[\"*.c\"]}}");
    Workspace w = Workspace::load(b.paths, b.fs, b.fs);
    Request r;
    r.project = "C:\\p\\demo.pro";
    r.configuration = "Debug";
    r.target = "box";
    Plan p = w.plan(r);
    BuildLog log(w, r, p, "Test 1.0", "01-01-2026 00:00:00");
    std::string planned = log.planned();
    check(planned.find("GRIDE build   demo   Debug   box   01-01-2026 00:00:00   Test 1.0") == 0, "log: the head\n" + planned);
    check(planned.find("files         Sources: *.c -> main.c, util.c") != std::string::npos, "log: what each pattern gave");
    check(planned.find("[1/4] compile   main.c") != std::string::npos && planned.find("[3/4] assemble  main.s, util.s") != std::string::npos &&
          planned.find("[4/4] link      demo.exe") != std::string::npos, "log: the assembles of a build are one numbered item");
    check(planned.find("      cc.exe -S main.c -o obj\\Debug\\Box\\main.s") != std::string::npos, "log: a program by its name, a file from the project's folder");

    std::vector<std::string> lines = log.begin();
    check(lines.size() == 4, "log: begin gives the head");
    check(log.finished(1, 0, "util.c(3): warning: unused\r\n", 0.1).empty(), "log: a step that ends before an earlier one releases nothing");
    lines = log.finished(0, 0, "", 0.2);
    check(lines.size() == 5 && startsWith(lines[0], "[1/4] compile   main.c") && startsWith(lines[2], "[2/4] compile   util.c") &&
          lines[4] == "      util.c(3): warning: unused", "log: then both are released, in the plan's order, each in one piece");
    check(log.finished(3, 0, "", 0.1).empty() && log.finished(2, 0, "", 0.1).size() == 3, "log: an item of two steps is released when both have ended");
    lines = log.finished(4, 0, "", 0.3);
    check(lines.size() == 2, "log: the link");
    lines = log.end();
    check(lines.size() == 1 && lines[0] == "built         out\\Debug\\Box\\demo.exe   0 errors, 1 warning   0.8 s", "log: the closing line: " + (lines.empty() ? std::string() : lines[0]));
    check(log.warnings() == 1 && log.errors() == 0, "log: the counts");
    check(b.fs.isFile("C:\\p\\obj\\Debug\\Box\\demo.build.log"), "log: kept as <project>.build.log in the object folder");
    std::string kept;
    b.fs.read("C:\\p\\obj\\Debug\\Box\\demo.build.log", kept);
    check(kept == log.text(), "log: the file is the text that was released");

    BuildLog second(w, r, p, "Test 1.0", "01-01-2026 00:00:01");
    second.begin();
    check(b.fs.isFile("C:\\p\\obj\\Debug\\Box\\demo.build.previous.log"), "log: the build before is kept as .build.previous.log");
    second.finished(0, 1, "main.c(1): error: no\n", 0.1);
    lines = second.end();
    check(lines.size() == 4 && startsWith(lines[0], "not run       [2/4] compile   util.c") && startsWith(lines[3], "failed        1 error, 0 warnings"),
          "log: a failed step ends the log with the steps that were not run named: " + join(lines, " / "));
    // the dry run: the build as a script, and nothing touched
    {
        Fake before = b.fs;
        BuildLog dry(w, r, p, "Test 1.0", "t");
        std::string text = dry.dryRun();
        Words said = split(replaceAll(text, "\r", ""), "\n");
        check(!said.empty() && startsWith(said[0], "DRY RUN ONLY"), "dry run: the first line says so: " + (said.empty() ? std::string() : said[0]));
        check(!said.empty() && said.back() == "DRY RUN ONLY - end", "dry run: and the last");
        check(text.find("      C:\\gride\\cc.exe -S C:\\p\\main.c -o C:\\p\\obj\\Debug\\Box\\main.s -arch box -g -D_DEBUG=1\r\n") != std::string::npos,
              "dry run: a command is written whole, the program by its path and every file by its path\n" + text);
        check(text.find("as.exe C:\\p\\obj\\Debug\\Box\\util.s") != std::string::npos && text.find("ld.exe C:\\p\\obj\\Debug\\Box\\main.o") != std::string::npos,
              "dry run: compile, assemble and link are all there");
        check(text.find("folder        C:\\p") != std::string::npos && text.find("would build   C:\\p\\out\\Debug\\Box\\demo.exe") != std::string::npos,
              "dry run: the folder the commands run in, and what they would build");
        check(b.fs.files.size() == before.files.size() && b.fs.clock == before.clock, "dry run: nothing is written");
        check(dry.text().empty(), "dry run: and nothing is released as a build's log");
    }
    // commands off the screen: the lines handed back leave them out, the kept text has them, a failure shows its own
    {
        BuildLog quiet(w, r, p, "Test 1.0", "t");
        quiet.showCommands(false);
        quiet.begin();
        std::vector<std::string> got = quiet.finished(0, 0, "", 0.1);
        check(got.size() == 1 && startsWith(got[0], "[1/4] compile   main.c"), "log: with commands off the screen a step hands back its line alone");
        check(quiet.text().find("      cc.exe -S main.c") != std::string::npos, "log: and the kept text still has the command");
        got = quiet.finished(1, 2, "util.c(1): error: no\n", 0.1);
        check(got.size() == 4 && got[3].find("cc.exe -S util.c") != std::string::npos, "log: a step that fails shows its command after all: " + join(got, " / "));
    }
    BuildLog third(w, r, p, "Test 1.0", "t");
    third.begin();
    lines = third.upToDate(0);
    check(lines.size() == 1 && lines[0] == "[1/4] compile   main.c - up to date", "log: a step that was up to date says so in its place: " + (lines.empty() ? std::string() : lines[0]));
}

// which steps need to run
void testStamps() {
    Bench b;
    b.fs.put("C:\\p\\main.c"); b.fs.put("C:\\p\\util.c"); b.fs.put("C:\\p\\a.h");
    std::string pro = "{\"name\":\"demo\",\"groups\":{\"Sources\":[\"*.c\"],\"Headers\":[\"*.h\"]}}";
    b.fs.put("C:\\p\\demo.pro", pro);
    Workspace w = Workspace::load(b.paths, b.fs, b.fs);
    Request r;
    r.project = "C:\\p\\demo.pro";
    r.configuration = "Debug";
    r.target = "box";
    Plan p = w.plan(r);
    check(p.steps.size() == 5, "stamps: the plan");
    if (p.steps.size() != 5) return;
    {
        Stamps s(w, r, p);
        bool none = true;
        for (int i = 0; i < 5; ++i) if (s.upToDate(i)) none = false;
        check(none, "stamps: nothing is up to date before the first build");
        for (int i = 0; i < 5; ++i) { b.fs.put(p.steps[i].outputs[0]); s.ran(i, true); }     // the build, in the plan's order
        s.save();
    }
    check(b.fs.isFile("C:\\p\\obj\\Debug\\Box\\demo.stamps"), "stamps: kept beside the objects");
    {
        Stamps s(w, r, p);
        bool all = true;
        for (int i = 0; i < 5; ++i) if (!s.upToDate(i)) all = false;
        check(all, "stamps: after a build every step is up to date");
    }
    b.fs.put("C:\\p\\util.c");                                 // one source edited
    {
        Stamps s(w, r, p);
        check(s.upToDate(0) && !s.upToDate(1), "stamps: an edited source's compile runs, the other's does not");
        b.fs.put(p.steps[1].outputs[0]); s.ran(1, true);       // util.s rewritten
        check(s.upToDate(2) && !s.upToDate(3), "stamps: then its assembly runs, the other's does not");
        b.fs.put(p.steps[3].outputs[0]); s.ran(3, true);
        check(!s.upToDate(4), "stamps: and the link");
        b.fs.put(p.steps[4].outputs[0]); s.ran(4, true);
        s.save();
    }
    b.fs.put("C:\\p\\a.h");                                    // a header edited: every compile runs
    {
        Stamps s(w, r, p);
        check(!s.upToDate(0) && !s.upToDate(1), "stamps: an edited header makes every compile run");
        for (int i = 0; i < 5; ++i) { b.fs.put(p.steps[i].outputs[0]); s.ran(i, true); }
        s.save();
    }
    b.fs.put("C:\\p\\demo.pro", "{\"name\":\"demo\",\"groups\":{\"Sources\":[\"*.c\"],\"Headers\":[\"*.h\"]},\"options\":{\"debug\":{\"cc.opt\":\"-O1\"}}}");
    Plan changed = w.plan(r);
    {
        Stamps s(w, r, changed);
        check(!s.upToDate(0) && !s.upToDate(1), "stamps: a changed Compiler Option makes every compile run");
        check(s.upToDate(2), "stamps: and by itself no assembly, whose command is the same");
    }
    b.fs.put("C:\\p\\demo.pro", pro);
    {
        Stamps s(w, r, p);
        check(s.upToDate(0), "stamps: the option put back, the compile is up to date again");
        s.ran(0, false);
        s.save();
    }
    {
        Stamps s(w, r, p);
        check(!s.upToDate(0), "stamps: a step that failed is not up to date the next time");
    }
    b.fs.files.erase(Fake::key(p.steps[4].outputs[0]));
    {
        Stamps s(w, r, p);
        check(!s.upToDate(4), "stamps: a program that is gone is linked again");
    }
    b.fs.put("C:\\p\\extra.c");                                // a file a pattern now takes
    Plan more = w.plan(r);
    {
        Stamps s(w, r, more);
        check(more.steps.size() == 7 && !s.upToDate(6), "stamps: a new file of a pattern changes the link's stamp");
    }
}

// ---- the census: Black's recorded builds against the block's plans ----

struct Recorded { std::string program; Words argv; };

std::string canon(const std::string &program, const Words &argv) {
    std::string r = lower(leaf(program));
    for (size_t i = 0; i < argv.size(); ++i) r += "\x1f" + lower(argv[i]);
    return r;
}

std::string shownLine(const std::string &c) { return replaceAll(c, "\x1f", " "); }

int census(const std::string &baseline, const std::string &examples, const std::string &tools, bool verbose) {
    FileSystem &fs = systemFileSystem();
    Paths paths = defaultPaths(tools, "Black");
    paths.userToolchain.clear();
    Workspace w = Workspace::load(paths, fs, systemEnvironment());
    std::vector<Problem> wrong = w.problems();
    for (size_t i = 0; i < wrong.size(); ++i) std::printf("%s: %s\n", wrong[i].severity.c_str(), wrong[i].text.c_str());

    Words names = fs.list(baseline);
    std::sort(names.begin(), names.end());
    int same_ = 0, fixed = 0, different = 0, skipped = 0, linkFiles = 0, linkFilesSame = 0;
    std::map<std::string, int> removedTokens, envOnlyRecorded;
    for (size_t n = 0; n < names.size(); ++n) {
        if (!endsWith(names[n], ".txt")) continue;
        std::string text;
        fs.read(combine(baseline, names[n]), text);
        Words lines = split(replaceAll(text, "\r", ""), "\n", true);
        std::string project, config, target;
        std::vector<Recorded> recorded;
        std::string writtenFile, written;
        for (size_t i = 0; i < lines.size(); ++i) {
            const std::string &l = lines[i];
            if (startsWith(l, "project:")) { size_t a = l.find('('), z = l.rfind(')'); if (a != std::string::npos && z != std::string::npos) project = l.substr(a + 1, z - a - 1); }
            else if (startsWith(l, "configuration:")) config = trim(l.substr(14));
            else if (startsWith(l, "target:")) target = trim(l.substr(7));
            else if (startsWith(l, "file written for a tool:")) writtenFile = trim(l.substr(24));
            else if (startsWith(l, "    | ")) written += l.substr(6) + "\r\n";
            else if (startsWith(l, "    |")) written += "\r\n";
        }
        if (!endsWith(project, ".pro")) { ++skipped; continue; }           // a CCS project: the design's step 8
        std::string pro = combine(parent(parent(examples)), project);      // examples\projects\x\y.pro, from the folder above examples
        std::string folder = parent(pro);
        for (size_t i = 0; i < lines.size(); ++i) {
            if (!startsWith(lines[i], "    cmd: ")) continue;
            std::string cmd = lines[i].substr(9);
            // the line is cut into words while the two folders are still tokens: a recording made in a folder with
            // no blank in its name has no quotes around a path that this machine's folder would need them for
            Words all = cutWords(cmd);
            for (size_t k = 0; k < all.size(); ++k) all[k] = replaceAll(replaceAll(all[k], "<proj>", folder), "<bin>", tidy(tools));
            if (all.empty()) continue;
            Recorded r;
            r.program = all[0];
            r.argv.assign(all.begin() + 1, all.end());
            recorded.push_back(r);
        }
        Request request;
        request.project = pro;
        request.configuration = config;
        request.target = target;
        request.action = Rebuild;
        Plan plan = w.plan(request);

        std::multiset<std::string> theirs, ours, theirsFixed;
        for (size_t i = 0; i < recorded.size(); ++i) theirs.insert(canon(recorded[i].program, recorded[i].argv));
        for (size_t i = 0; i < plan.steps.size(); ++i) ours.insert(canon(plan.steps[i].program, plan.steps[i].argv));
        // the two listed fixes: Black's invented configuration gave every project -I<project>\inc and -DDEBUG / -DNDEBUG
        std::string inc = lower("-I" + combine(folder, "inc"));
        for (size_t i = 0; i < recorded.size(); ++i) {
            Words kept;
            for (size_t k = 0; k < recorded[i].argv.size(); ++k) {
                std::string a = lower(recorded[i].argv[k]);
                if (a == inc || a == "-ddebug" || a == "-dndebug") { ++removedTokens[a == inc ? "-I<project>\\inc" : recorded[i].argv[k]]; continue; }
                kept.push_back(recorded[i].argv[k]);
            }
            theirsFixed.insert(canon(recorded[i].program, kept));
        }
        std::string verdict;
        if (ours == theirs) { ++same_; verdict = "same"; }
        else if (ours == theirsFixed) { ++fixed; verdict = "same after the listed fixes"; }
        else { ++different; verdict = "DIFFERENT"; }
        if (!writtenFile.empty()) {
            ++linkFiles;
            if (plan.files.size() == 1 && plan.files[0].second == written &&
                samePath(plan.files[0].first, replaceAll(writtenFile, "<proj>", folder))) ++linkFilesSame;
            else if (verdict != "DIFFERENT") { verdict = "DIFFERENT (the written file)"; ++different; if (ours == theirs) --same_; else --fixed; }
        }
        if (verbose || startsWith(verdict, "DIFFERENT")) std::printf("%-58s %s\n", names[n].c_str(), verdict.c_str());
        if (startsWith(verdict, "DIFFERENT")) {
            for (size_t i = 0; i < plan.problems.size(); ++i) std::printf("      %s: %s\n", plan.problems[i].severity.c_str(), plan.problems[i].text.c_str());
            int shown = 0;
            for (std::multiset<std::string>::iterator it = theirsFixed.begin(); it != theirsFixed.end() && shown < 3; ++it)
                if (!ours.count(*it)) { std::printf("      Black only: %s\n", shownLine(*it).c_str()); ++shown; }
            shown = 0;
            for (std::multiset<std::string>::iterator it = ours.begin(); it != ours.end() && shown < 3; ++it)
                if (!theirsFixed.count(*it)) { std::printf("      block only: %s\n", shownLine(*it).c_str()); ++shown; }
        }
    }
    std::printf("\ncensus of Black, %d builds of .pro projects (%d of CCS projects left for step 8):\n", same_ + fixed + different, skipped);
    std::printf("  %3d  the same commands\n  %3d  the same after the listed fixes\n  %3d  different\n", same_, fixed, different);
    std::printf("  %3d of %d linker command files the same text at the same place\n", linkFilesSame, linkFiles);
    for (std::map<std::string, int>::iterator it = removedTokens.begin(); it != removedTokens.end(); ++it)
        std::printf("  fix: %-18s no longer given, %d times\n", it->first.c_str(), it->second);
    return different ? 1 : 0;
}

// every example's name lists written as patterns: does each group give the files it gave?
int patterns(const std::string &examples, const std::string &toolchain) {
    Definitions defs = definitionsIn(toolchain);
    FileSystem &fs = systemFileSystem();
    Words pros, folders(1, tidy(examples));
    for (size_t f = 0; f < folders.size(); ++f) {
        Words names = fs.list(folders[f]);
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i][names[i].size() - 1] == '\\') {
                std::string sub = names[i].substr(0, names[i].size() - 1);
                if (!same(sub, "obj") && !same(sub, "out") && !same(sub, "build")) folders.push_back(combine(folders[f], sub));
            } else if (endsWith(names[i], ".pro")) pros.push_back(combine(folders[f], names[i]));
        }
    }
    std::sort(pros.begin(), pros.end());
    int projects = 0, sameFiles = 0, differ = 0, entriesBefore = 0, entriesAfter = 0, reordered = 0, untouched = 0;
    for (size_t i = 0; i < pros.size(); ++i) {
        Project p;
        std::string why;
        if (!readProject(pros[i], fs, p, why)) { std::printf("%s: %s\n", pros[i].c_str(), why.c_str()); ++differ; continue; }
        ++projects;
        Words never;
        never.push_back(combine(p.folder, "obj")); never.push_back(combine(p.folder, "out")); never.push_back(combine(p.folder, "build"));
        Project q = p;
        q.groups = compacted(p, fs, never, defs);
        ProjectFiles before = resolveFiles(p, fs, never), after = resolveFiles(q, fs, never);
        bool ok = true, order = false, changed = false;
        for (size_t g = 0; g < p.groups.size(); ++g) {
            entriesBefore += static_cast<int>(p.groups[g].entries.size());
            entriesAfter += static_cast<int>(q.groups[g].entries.size());
            if (p.groups[g].entries != q.groups[g].entries) changed = true;
            Words a, b;
            for (size_t k = 0; k < before.files.size(); ++k) if (before.files[k].group == p.groups[g].name) a.push_back(lower(before.files[k].relative));
            for (size_t k = 0; k < after.files.size(); ++k) if (after.files[k].group == p.groups[g].name) b.push_back(lower(after.files[k].relative));
            if (a != b) order = true;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            if (a != b) { ok = false; std::printf("%s: group %s gives other files as patterns\n", leaf(pros[i]).c_str(), p.groups[g].name.c_str()); }
        }
        if (!ok) { ++differ; continue; }
        ++sameFiles;
        if (!changed) ++untouched;
        if (order) ++reordered;
        std::string shown;
        for (size_t g = 0; g < q.groups.size(); ++g) shown += (g ? "  " : "") + q.groups[g].name + ": " + join(q.groups[g].entries, ", ");
        std::printf("%-22s %s%s\n", leaf(pros[i]).c_str(), shown.size() > 150 ? (shown.substr(0, 150) + " ...").c_str() : shown.c_str(), order ? "   [order changes]" : "");
    }
    std::printf("\n%d projects: %d give the same files written as patterns, %d do not; %d keep their names as they are;\n"
                "%d entries become %d; in %d the order of a group's files changes (by name instead of as written)\n",
                projects, sameFiles, differ, untouched, entriesBefore, entriesAfter, reordered);
    return differ ? 1 : 0;
}

// a .pro written back with its name lists as patterns, where nothing is lost by it
int rewrite(const std::string &file, const std::string &toolchain) {
    Definitions defs = definitionsIn(toolchain);
    FileSystem &fs = systemFileSystem();
    Project p;
    std::string why, text, error;
    if (!readProject(file, fs, p, why) || !fs.read(file, text)) { std::printf("%s\n", why.c_str()); return 1; }
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    Json root = Json::parse(text, error);
    Words never;
    never.push_back(combine(p.folder, "obj")); never.push_back(combine(p.folder, "out")); never.push_back(combine(p.folder, "build"));
    std::vector<Group> groups = compacted(p, fs, never, defs);
    Json all = root.get("groups");
    if (!all.is(Json::Object)) return 0;
    for (size_t g = 0; g < groups.size(); ++g) {
        Json entries = Json::array();
        for (size_t e = 0; e < groups[g].entries.size(); ++e) entries.push(Json::fromText(groups[g].entries[e]));
        Json value = all.get(groups[g].name);
        if (value.is(Json::Object)) value.set("files", entries); else value = entries;
        all.set(groups[g].name, value);
    }
    root.set("groups", all);
    return fs.write(file, root.document()) ? 0 : 1;
}

}

int main(int argc, char **argv) {
    if (argc >= 4 && std::strcmp(argv[1], "rewrite") == 0) return rewrite(argv[2], argv[3]);
    if (argc >= 5 && std::strcmp(argv[1], "census") == 0) return census(argv[2], argv[3], argv[4], argc > 5);
    if (argc >= 4 && std::strcmp(argv[1], "patterns") == 0) return patterns(argv[2], argv[3]);
    testWords();
    testFill();
    testPaths();
    testMerge();
    testSettings();
    testPatterns();
    testPlan();
    testLog();
    testStamps();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
