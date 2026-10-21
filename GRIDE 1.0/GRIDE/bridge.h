#ifndef GRIDE_BRIDGE_H
#define GRIDE_BRIDGE_H

/* The C seam both windows consume. A const char* answer is the bridge's own string, good until the
   next call on the same object - or, for one taking none, the next such call; copy it first. One
   thread at a time per GRIDEProject. The rest - errors, side effects - is README.md, "The seam". */
#ifdef __cplusplus
extern "C" {
#endif

enum {
    GRIDE_KIND_NORMAL = 0, GRIDE_KIND_KEYWORD, GRIDE_KIND_TYPE, GRIDE_KIND_STRING,
    GRIDE_KIND_CHAR, GRIDE_KIND_COMMENT, GRIDE_KIND_PREPROC, GRIDE_KIND_NUMBER,
    GRIDE_KIND_LABEL
};
enum { GRIDE_LANG_PLAIN = 0, GRIDE_LANG_C, GRIDE_LANG_CPP, GRIDE_LANG_SHALIMAR,
       GRIDE_LANG_ASM, GRIDE_LANG_JSON };

enum { GRIDE_DIALECT_C = 0, GRIDE_DIALECT_SHALIMAR };
enum { GRIDE_TOOL_AUTO = 0, GRIDE_TOOL_CC1, GRIDE_TOOL_MSVC, GRIDE_TOOL_SHC, GRIDE_TOOL_CXX,
       GRIDE_TOOL_CXX1 };
enum { GRIDE_CONFIG_DEBUG = 0, GRIDE_CONFIG_RELEASE };

void gride_watch_for_faults(const char* logPath);

char* gride_reindent(const char* text, int width, int tabs, int caseIndent,
                   int dialect);

char* gride_indent_after_newline(const char* text, int row, int col,
                               int width, int tabs, int caseIndent, int dialect);

char* gride_indent_for(const char* text, int row, int width, int tabs, int caseIndent,
                     int dialect);

void gride_free(char* what);

int gride_find_next(const char* text, const char* needle, int row, int col,
                  int* foundRow, int* foundCol);
int gride_find_previous(const char* text, const char* needle, int row, int col,
                      int* foundRow, int* foundCol);

char* gride_replace_all(const char* text, const char* needle, const char* with,
                      int* howMany);

const char* gride_settings_set_aside(void);

const char* gride_code_font(void);
int gride_remember_code_font(const char* described);

/* Win32 only: a Rich Edit's HWND. Elsewhere these do nothing. */
void gride_undo_suspend(void* windowHandle);
void gride_undo_resume(void* windowHandle);

int gride_language_for(const char* path);

int gride_dialect_for(int language);

int gride_highlight(const char* line, int language, int* state,
                  unsigned char* kinds, int kindsSize);

char* gride_describe_build(const char* assembly);

char* gride_debug_note(int kind, const char* arch);

typedef struct GRIDEProject GRIDEProject;

GRIDEProject* gride_project_new(void);
void gride_project_free(GRIDEProject* project);

/* A project's directory or its .pro. On failure the project is left not loaded; error says why,
   or is empty when there was no .pro there at all. */
int gride_project_load(GRIDEProject* project, const char* directory,
                     char* error, int errorSize);

const char* gride_project_name(GRIDEProject* project);
int gride_project_groups(GRIDEProject* project);
const char* gride_project_group_name(GRIDEProject* project, int group);
int gride_project_files(GRIDEProject* project, int group);
const char* gride_project_file(GRIDEProject* project, int group, int file);
const char* gride_project_absolute(GRIDEProject* project, const char* relative);

int gride_project_indent_width(GRIDEProject* project);
int gride_project_indent_tabs(GRIDEProject* project);
int gride_project_case_indent(GRIDEProject* project);
int gride_project_toolchain(GRIDEProject* project);

int gride_configuration(void);
void gride_remember_configuration(int config);
const char* gride_project_arch(GRIDEProject* project);
/* **A CCS project opened as it is** (src/ccs/ccsproject.h), when settings.json's "ccs" switch is
   on: gride_project_load takes its folder. Nothing of CCS's is written; the options dialog shows
   what was read and commits nothing; the report is the Messages line naming what GRIDE cannot honour
   (lines '\n' apart, the first for Messages), the mapping the dialog's text, and the
   configuration the one settings.json remembers for it (-1 unsaid), which the window keeps there. */
int gride_project_is_ccs(GRIDEProject* project);
/* **A CCS workspace** (src/ccs/ccsworkspace.h) is a folder of projects and never one: a window asks
   which, then opens <workspace>/<project>.pro, which holds the workspace and the project and nothing
   else. is_workspace says whether a folder is one; workspace_projects names its CCS projects, one per
   line ("" for none), valid until the next call; workspace_pro writes that .pro unless it is there and
   puts its path in file - 0, with the reason in error, when it cannot. */
int gride_ccs_is_workspace(const char* directory);
const char* gride_ccs_workspace_projects(const char* directory);
int gride_ccs_workspace_pro(const char* workspace, const char* project,
                           char* file, int fileSize, char* error, int errorSize);
const char* gride_project_ccs_report(GRIDEProject* project, int config);
const char* gride_project_ccs_mapping(GRIDEProject* project, int config);
int gride_project_ccs_configuration(GRIDEProject* project);
void gride_project_remember_configuration(GRIDEProject* project, int config);
/* Run file on one source of a several-source build: the count of sources it
   is one of, or 0 - then the window runs the project instead. */
int gride_project_runs_as_project(GRIDEProject* project, const char* source);
// The project's own target and compiler, set from the Target and Tools
// menus while it is open and written to its .pro at once; 0 with no project.
int gride_project_set_arch(GRIDEProject* project, const char* arch);
int gride_project_set_toolchain(GRIDEProject* project, int kind);

int gride_project_allows(const char* relative, char* why, int whySize);

const char* gride_group_for_file(const char* name);

const char* gride_project_suffix(void);
// The product's name, from product.h: the window's title, its message boxes, its logs.
const char* gride_product_name(void);
const char* gride_version(void);

// A project's header directories and libraries, as one ';'-separated line
// each, relative to the root as the file has them; setting one saves.
const char* gride_project_includes(GRIDEProject* project);
/* Build > Clean: what the open project's build made, and this process's scratch, removed - one full
   path a line, "" when there was nothing to remove. Sources, the .pro and CCS's files stay. */
const char* gride_project_clean(GRIDEProject* project);
/* The header a line or a selection names - "Quat.h" from #include "Quat.h", "cstdio" from
   #include <cstdio>, or a selection that is itself a file name - or "" when it names none. */
const char* gride_header_named(const char* text);
/* That header found as a compiler looks for it - beside the file it is named from, the project's
   folder and include paths, the shared include paths, the installation's include and lib - by its
   full path, or "" where none has it. */
const char* gride_find_header(GRIDEProject* project, const char* fromFile, const char* name);
const char* gride_project_libraries(GRIDEProject* project);
int gride_project_set_includes(GRIDEProject* project, const char* line);
int gride_project_set_libraries(GRIDEProject* project, const char* line);

// **Compiler Options** (src/options.h): the table both windows draw their tabbed dialog from, and a
// draft the dialog edits - begun from the open project's options, or the installation's where none
// is open; commit writes the draft to the .pro or settings.json, and a dialog cancelled just drops it.
enum { GRIDE_OPTION_CHECK = 0, GRIDE_OPTION_CHOICE, GRIDE_OPTION_TEXT };
int gride_option_count(void);
const char* gride_option_id(int index);
const char* gride_option_tab(int index);
const char* gride_option_label(int index);
int gride_option_control(int index);
const char* gride_option_choices(int index);   // a choice's values, '|' between them
const char* gride_option_hint(int index);
int gride_option_tab_count(void);
const char* gride_option_tab_name(int tab);
void gride_options_begin(GRIDEProject* project);
const char* gride_options_value(GRIDEProject* project, int config, const char* id);
void gride_options_set(GRIDEProject* project, int config, const char* id, const char* value);
void gride_options_reset(GRIDEProject* project, int config);
// Whether the option means anything for this target; gride_options_why says why not.
int gride_options_available(const char* id, const char* arch);
const char* gride_options_why(const char* id, const char* arch);
// The compiler and flags a tab's compiler is given for this configuration and target, from the draft.
const char* gride_options_preview(GRIDEProject* project, int config, int tab, const char* arch);
int gride_options_commit(GRIDEProject* project);

// The installation's settings.json, above bin/: where cxx1's headers (include) and cc1's (lib) are, and the vcvars64.bat named there if any.
const char* gride_install_file(void);
// Writes that file with the installer's defaults where there is none - ~/.gride/settings.json, from the installation's; 0 only when it is missing and cannot be written.
int gride_write_install_file_if_absent(void);
// The installation's include directories and libraries, one ';'-separated
// line each as settings.json has them; setting one writes the file.
const char* gride_includes(void);
const char* gride_libraries(void);
int gride_set_includes(const char* line);
/* The window's way of asking the build's question - "the project's own
   masm/link/lnk6x did not build it; use the native tools instead?" - a yes
   builds again through them (compile.h: setAskNative). Installed once, at
   start; the function is called on whichever thread is building. */
void gride_ask_native(int (*ask)(const char* question));
int gride_set_libraries(const char* line);
const char* gride_include_dir(void);
const char* gride_lib_dir(void);
int gride_remember_header_dirs(const char* include, const char* lib);
const char* gride_vcvars(void);
// The default compiler in settings.json, as an GRIDE_TOOL_* number, and
// the choice written back when one is made from the menu.
int gride_default_compiler(void);
int gride_default_indent_width(void);
int gride_default_indent_tabs(void);
int gride_remember_default_compiler(int kind);
int gride_remember_vcvars(const char* file);
// The assembler named for x86_64-windows, and the choice written back.
const char* gride_assembler(void);
int gride_remember_assembler(const char* file);
// TI's C6000 compiler directory and the runtime directory beside it, for a
// tms6747 build's .out, and the choice written back.
// The linkers named for x86_64-windows and tms6747, and the choices written back.
const char* gride_linker(void);
int gride_remember_linker(const char* file);
const char* gride_tilinker(void);
int gride_remember_tilinker(const char* file);
const char* gride_ti(void);
const char* gride_tilib(void);
int gride_remember_ti(const char* dir, const char* lib);

int gride_project_save_as(GRIDEProject* project, const char* file,
                            char* why, int whySize);

int gride_project_loaded(GRIDEProject* project);
const char* gride_project_root(GRIDEProject* project);
void gride_project_set_root(GRIDEProject* project, const char* path);

void gride_project_close(GRIDEProject* project);
const char* gride_project_relative(GRIDEProject* project, const char* path);

// kind is the compiler chosen (GRIDE_TOOL_*): a name with no extension
// gets that compiler's, or with the choice on automatic the project's usual.
int gride_create_file(GRIDEProject* project, const char* relative, const char* group, int kind);
int gride_rename_file(GRIDEProject* project, const char* fromAbsolute, const char* toRelative);
int gride_delete_file(GRIDEProject* project, const char* absolute);
int gride_move_to_group(GRIDEProject* project, const char* absolute, const char* group);
int gride_add_existing(GRIDEProject* project, const char* absolute, const char* group);
// A file just saved under the project's root joins it; 0 when that did not apply, and nothing to say then.
int gride_adopt_saved(GRIDEProject* project, const char* absolute);
// Whether the project lists this file, in any group.
int gride_project_holds(GRIDEProject* project, const char* absolute);
// The file the project opens with, relative to its root - its own choice,
// else the one defining main, else the first - and remembering the one in
// front when the project is closed or the window left.
const char* gride_project_file_to_open(GRIDEProject* project);
int gride_remember_open(GRIDEProject* project, const char* absolute);

int gride_remove_from_project(GRIDEProject* project, const char* absolute);

int gride_begin_from_what_is_there(GRIDEProject* project, const char* directory);
const char* gride_last_project(void);
int gride_remember_project(const char* directory);
// The last three projects opened, most recent first; empty past the end.
const char* gride_recent_project(int index);
const char* gride_recent_file(int index);
int gride_remember_file(const char* path);
const char* gride_demo_directory(void);

int gride_begin_project(GRIDEProject* project, const char* directory, const char* name,
                      const char* firstFile);
int gride_save_project(GRIDEProject* project);

const char* gride_outcome_message(GRIDEProject* project);
const char* gride_outcome_path(GRIDEProject* project);

/* One diagnostic read from compiler output - one line, or two for cc1's preprocessor - by the core's
   own parser; 1 when there is one. *file and *message hold until the next call. Any one thread. */
int gride_parse_diagnostic(const char* text, const char* source, int* line, int* column,
                          const char** file, const char** message);
/* The three compilers' names as people type them; their programs are these with .exe. */
enum { GRIDE_COMPILER_C = 0, GRIDE_COMPILER_CPP, GRIDE_COMPILER_SHALIMAR };
const char* gride_compiler_name(int which);

const char* gride_arch(int index);
int gride_arch_count(void);
const char* gride_toolchain_name(int kind);

const char* gride_language_name(int language);
const char* gride_config_name(int config);
int gride_resolve(int toolchainKind, int language);
int gride_can_compile(int kind, int language);
const char* gride_refusal(int kind, int language);
int gride_uses_arch(int kind);

int gride_runs_here(int kind, const char* arch);
const char* gride_why_not_run(int kind, const char* arch);
const char* gride_host_arch(void);

const char* gride_shown_command(GRIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind,
                              const char* source, int language, const char* arch,
                              int config);

int gride_converts_from(int language, int* toShalimar);
char* gride_find_converter(void);
char* gride_converted_name(const char* source, int toShalimar);

typedef struct GRIDEConversion GRIDEConversion;

GRIDEConversion* gride_convert(const char* converter, const char* source,
                                   const char* output, int toShalimar);
void gride_conversion_free(GRIDEConversion* made);
int gride_conversion_ran(GRIDEConversion* made);
int gride_conversion_ok(GRIDEConversion* made);
const char* gride_conversion_produced(GRIDEConversion* made);
const char* gride_conversion_output(GRIDEConversion* made);

typedef struct GRIDEBuild GRIDEBuild;

GRIDEBuild* gride_build(GRIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind, const char* source,
                    int language, const char* arch, int config);

int gride_project_builds(GRIDEProject* project);
/* Works out what the project builds; the gride_project_target_* and _part_* answers below are its,
   until it is called again. gride_build_target and gride_project_debug_plan call it themselves. */
int gride_project_target_ready(GRIDEProject* project);
const char* gride_project_target_why(GRIDEProject* project);
const char* gride_project_target_detail(GRIDEProject* project);
int gride_project_target_language(GRIDEProject* project);
int gride_project_target_sources(GRIDEProject* project);

int gride_project_target_parts(GRIDEProject* project);
const char* gride_project_part_group(GRIDEProject* project, int index);
int gride_project_part_language(GRIDEProject* project, int index);
int gride_project_part_toolchain(GRIDEProject* project, int index, const char* cc1,
                               const char* cl, const char* shc, const char* cxx1, int kind);
const char* gride_project_target_source(GRIDEProject* project, int index);
const char* gride_project_target_program(GRIDEProject* project);

int gride_project_debug_plan(GRIDEProject* project, const char* cc1, const char* cl,
                           const char* shc, const char* cxx1, int kind, const char* arch);
int gride_project_debug_kind(GRIDEProject* project);
const char* gride_project_why_not_debug(GRIDEProject* project);
int gride_project_blind_groups(GRIDEProject* project);
const char* gride_project_blind_group(GRIDEProject* project, int index);

GRIDEBuild* gride_build_target(GRIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1,
                           int kind, const char* arch, int config);
void gride_build_free(GRIDEBuild* built);

int gride_build_ok(GRIDEBuild* built);
/* What the build made and what runs: the program, or <program>.vm for the emulated target; and
   whether it is Shalimar, which the emulator runs beside its runtime. Run it with gride_run_made_start. */
const char* gride_build_made(GRIDEBuild* built);
int gride_build_made_shalimar(GRIDEBuild* built);
const char* gride_build_output(GRIDEBuild* built);
const char* gride_build_assembly(GRIDEBuild* built);
int gride_build_assembly_lines(GRIDEBuild* built);
int gride_build_has_error(GRIDEBuild* built);

const char* gride_build_error_file(GRIDEBuild* built);
int gride_build_error_line(GRIDEBuild* built);
int gride_build_error_column(GRIDEBuild* built);
const char* gride_build_error_message(GRIDEBuild* built);

typedef struct GRIDERan GRIDERan;

GRIDERan* gride_run(GRIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind, const char* source,
                int language, const char* arch, int config);

GRIDERan* gride_run_built(const char* program);
void gride_run_free(GRIDERan* ran);

int gride_ran_built(GRIDERan* ran);
int gride_ran_ran(GRIDERan* ran);
int gride_ran_status(GRIDERan* ran);
const char* gride_ran_output(GRIDERan* ran);
int gride_ran_has_error(GRIDERan* ran);
int gride_ran_error_line(GRIDERan* ran);
int gride_ran_error_column(GRIDERan* ran);
const char* gride_ran_error_message(GRIDERan* ran);
/* The file the error is in - a header, perhaps, and not the source that was built. */
const char* gride_ran_error_file(GRIDERan* ran);

/* ---- a program that runs while the window watches (README.md, "Input, and Stop") -------------
   GRIDEOutput runs on a worker thread: marshal to the window's own and never wait there for a thread
   that may call gride_running_free. Bytes are not kept after the call; bytes NULL means it is over. */
/* The program's terminal echoes what is sent, so the window does not. On Windows it is a pseudo-
   console, whose one output carries stderr as GRIDE_STREAM_OUT; before 1809, pipes, and no echo. */
enum { GRIDE_STREAM_OUT = 0, GRIDE_STREAM_ERR = 1, GRIDE_STREAM_BUILD = 2 };
typedef void (*GRIDEOutput)(void* user, const char* bytes, int size, int stream);

typedef struct GRIDERunning GRIDERunning;

/* gride_run's build, its lines as GRIDE_STREAM_BUILD, then the program with a real input; the program
   is removed afterwards. The project is read here, on this thread, and not again. */
GRIDERunning* gride_run_start(GRIDEProject* project, const char* cc1, const char* cl, const char* shc,
                            const char* cxx1, int kind, const char* source, int language,
                            const char* arch, int config, GRIDEOutput onOutput, void* user);
/* A program already built, as gride_run_built runs it; it is left where it is. */
GRIDERunning* gride_run_built_start(const char* program, GRIDEOutput onOutput, void* user);
/* The same for what gride_build_made named, Shalimar or not. */
GRIDERunning* gride_run_made_start(const char* program, int shalimar, GRIDEOutput onOutput, void* user);
/* The same two, handing the program a command line - Build > Command-line arguments - split as a shell
   would at spaces outside quotes; a word naming a file under `base` (the project, or the file's folder)
   goes by its full name. */
GRIDERunning* gride_run_made_start_line(const char* program, int shalimar, const char* line, const char* base,
                                      GRIDEOutput onOutput, void* user);
GRIDERunning* gride_run_start_line(GRIDEProject* project, const char* cc1, const char* cl, const char* shc,
                                 const char* cxx1, int kind, const char* source, int language,
                                 const char* arch, int config, const char* line, const char* base,
                                 GRIDEOutput onOutput, void* user);
/* The project's command-line arguments as one line, and setting them from one: kept in its .pro, or for a
   CCS project in GRIDE's own state. The line said before a run: "$ program arguments". */
char* gride_project_arguments(GRIDEProject* project);
int gride_project_set_arguments(GRIDEProject* project, const char* line);
char* gride_project_run_line(GRIDEProject* project, const char* program);
/* "$ program line", the program by its name in the project where it is in one. */
char* gride_run_line(GRIDEProject* project, const char* program, const char* line);

/* **What a gride_*_start runs, for tms6747 (R5).** Said just before the build, and taken by the start:
   GRIDE_RUN_PROGRAM on tms6747 is the build's linked <program>.out on sim6747, as GRIDE_RUN_SIMULATOR
   says outright - or vm6747's when the .pro says emulateOnly; GRIDE_RUN_EMULATOR (Build > Emulate on
   vm6747) runs the assembly on vm6747 and the build links no .out; GRIDE_RUN_VERIFY runs both, captured -
   status 0 they agree, 1 they differ, 2 one could not run. Why not is said on the output when it cannot. */
enum { GRIDE_RUN_PROGRAM = 0, GRIDE_RUN_SIMULATOR = 1, GRIDE_RUN_VERIFY = 2, GRIDE_RUN_EMULATOR = 3 };
void gride_run_next(int runner);
/* 1 when sim6747 is beside the editor (or named by $SIM6747). */
int gride_simulator_here(void);

/* Bytes to its input as they are - a line wants its "\n"; held until it starts. 0 once it has
   ended or its input was closed. Any thread, as are the next three. */
int gride_running_send(GRIDERunning* running, const char* bytes, int size);
/* End of input: its next read sees end of file. */
void gride_running_close_input(GRIDERunning* running);
/* Ends the build, or the program and all it started; the run ends as usual, stopped. */
void gride_running_stop(GRIDERunning* running);
/* Up to timeoutMs (negative: for ever) for the run to end; 1 when it has. */
int gride_running_wait(GRIDERunning* running, int timeoutMs);

/* Answered once done is 1, 0 or "" before; the strings live as long as the GRIDERunning. */
int gride_running_done(GRIDERunning* running);
int gride_running_built(GRIDERunning* running);   /* there was a program to run */
int gride_running_ran(GRIDERunning* running);     /* and it started */
int gride_running_status(GRIDERunning* running);  /* exit code, or 128 + the signal */
int gride_running_stopped(GRIDERunning* running); /* Stop ended it, or its build */
const char* gride_running_build_output(GRIDERunning* running);
int gride_running_has_error(GRIDERunning* running);
const char* gride_running_error_file(GRIDERunning* running);
int gride_running_error_line(GRIDERunning* running);
int gride_running_error_column(GRIDERunning* running);
const char* gride_running_error_message(GRIDERunning* running);

/* Stops a run still going, joins its worker, lets it go. Not from inside GRIDEOutput. */
void gride_running_free(GRIDERunning* running);

/* Stop for a build, any thread: every compiler, linker and converter the calls here are running is
   killed, and each call returns failed with "[stopped]" in its output and its _stopped answer 1.
   A blocking gride_run or gride_run_built has its program ended the same way. */
void gride_cancel_builds(void);
int gride_build_stopped(GRIDEBuild* built);
int gride_ran_stopped(GRIDERan* ran);
int gride_conversion_stopped(GRIDEConversion* made);

const char* gride_shown_run_command(GRIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind,
                                  const char* source, int language, const char* arch,
                                  int config);

char* gride_about(void);
/* Edit > Find in Files: a heading line, then a line a hit - "<file>:<line>:<col>: <the line>", or for a
   name search "<file>" alone - at most 2000 of them. Freed by the caller. */
// Ends a Find in Files running on another thread at its next file; its answer then says "stopped".
void gride_find_stop(void);
char* gride_find_in_files(const char* text, const char* folder, const char* patterns,
                         int matchCase, int wholeWord, int subfolders, int namesOnly);
// About without its last lines, and those lines alone: the macOS panel puts them in its line under the credits.
char* gride_about_credits(void);
char* gride_about_stamp(void);
/* Help > Environment: every tool, header directory and library in force, and where each came from. */
char* gride_environment(void);

typedef struct GRIDEProgram GRIDEProgram;

GRIDEProgram* gride_build_program(GRIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind, const char* source,
                              int language, const char* arch, int config);
/* Deletes the built program from the disk, and what was built beside it, then lets it go. */
void gride_program_free(GRIDEProgram* built);

int gride_program_ok(GRIDEProgram* built);
const char* gride_program_path(GRIDEProgram* built);
const char* gride_program_output(GRIDEProgram* built);
int gride_program_has_error(GRIDEProgram* built);
int gride_program_error_line(GRIDEProgram* built);
int gride_program_error_column(GRIDEProgram* built);
const char* gride_program_error_message(GRIDEProgram* built);
const char* gride_program_error_file(GRIDEProgram* built);

int gride_debugger_for(int kind, const char* arch);
const char* gride_debugger_name(int kind);
const char* gride_no_debugger_because(int kind, const char* arch);

int gride_debugger_stops_itself(int kind);

const char* gride_release_cannot_stop(int kind);

const char* gride_why_it_did_not_start(int kind, const char* arch);

typedef struct GRIDEDebugger GRIDEDebugger;

GRIDEDebugger* gride_debugger_new(void);
void gride_debugger_free(GRIDEDebugger* debugger);

int gride_debugger_start(GRIDEDebugger* debugger, int kind, const char* arch,
                       const char* program);
int gride_debugger_running(GRIDEDebugger* debugger);
void gride_debugger_stop(GRIDEDebugger* debugger);

int gride_debugging_shalimar(GRIDEDebugger* debugger);

const char* gride_locals_none_because(GRIDEDebugger* debugger);
const char* gride_cannot_watch(GRIDEDebugger* debugger);
const char* gride_cannot_walk_stack(GRIDEDebugger* debugger);

int gride_debugger_break(GRIDEDebugger* debugger, const char* file, int line);
int gride_debugger_clear(GRIDEDebugger* debugger);

void gride_debugger_run(GRIDEDebugger* debugger);
void gride_debugger_resume(GRIDEDebugger* debugger);
void gride_debugger_step_over(GRIDEDebugger* debugger);
void gride_debugger_step_into(GRIDEDebugger* debugger);
void gride_debugger_step_out(GRIDEDebugger* debugger);

int gride_stop_stopped(GRIDEDebugger* debugger);
int gride_stop_exited(GRIDEDebugger* debugger);
int gride_stop_status(GRIDEDebugger* debugger);
const char* gride_stop_file(GRIDEDebugger* debugger);
int gride_stop_line(GRIDEDebugger* debugger);
const char* gride_stop_function(GRIDEDebugger* debugger);

const char* gride_stop_said(GRIDEDebugger* debugger);

const char* gride_stop_output(GRIDEDebugger* debugger);

int gride_stop_no_source(GRIDEDebugger* debugger);

int gride_locals_count(GRIDEDebugger* debugger);
const char* gride_local_name(GRIDEDebugger* debugger, int index);
const char* gride_local_type(GRIDEDebugger* debugger, int index);
const char* gride_local_value(GRIDEDebugger* debugger, int index);
// Where it lives, "0x...", or "" in a register or for Shalimar - the grids' Address column.
const char* gride_local_address(GRIDEDebugger* debugger, int index);

const char* gride_local_text(GRIDEDebugger* debugger, int index);
int gride_locals_on_line(GRIDEDebugger* debugger, const char* line);
int gride_set_variable(GRIDEDebugger* debugger, const char* name, const char* value);
const char* gride_set_complaint(GRIDEDebugger* debugger);

void gride_watch_add(GRIDEDebugger* debugger, const char* expression);
int gride_watch_count(GRIDEDebugger* debugger);
const char* gride_watch_text(GRIDEDebugger* debugger, int index);
const char* gride_watch_expression(GRIDEDebugger* debugger, int index);
// A watch's fields apart, for the grid: its value (or the refusal), 1 when it is a value, and its address.
const char* gride_watch_value(GRIDEDebugger* debugger, int index);
int gride_watch_ok(GRIDEDebugger* debugger, int index);
const char* gride_watch_address(GRIDEDebugger* debugger, int index);
const char* gride_watch_type(GRIDEDebugger* debugger, int index);
int gride_watch_on_line(GRIDEDebugger* debugger, const char* line);
void gride_watch_set(GRIDEDebugger* debugger, int index, const char* expression);

int gride_stack_count(GRIDEDebugger* debugger);
const char* gride_stack_function(GRIDEDebugger* debugger, int index);
const char* gride_stack_file(GRIDEDebugger* debugger, int index);
int gride_stack_line(GRIDEDebugger* debugger, int index);

const char* gride_stack_text(GRIDEDebugger* debugger, int index);
int gride_stack_on_line(GRIDEDebugger* debugger, const char* line);

int gride_debugger_look_at(GRIDEDebugger* debugger, int which);

const char* gride_stop_line_text(const char* file, int line, const char* function);
int gride_looking_at(GRIDEDebugger* debugger);
const char* gride_looking_text(GRIDEDebugger* debugger);

#ifdef __cplusplus
}
#endif

#endif
