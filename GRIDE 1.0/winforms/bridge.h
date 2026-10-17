#ifndef RIDE_BRIDGE_H
#define RIDE_BRIDGE_H

/* The C seam both windows consume. A const char* answer is the bridge's own string, good until the
   next call on the same object - or, for one taking none, the next such call; copy it first. One
   thread at a time per RIDEProject. The rest - errors, side effects - is README.md, "The seam". */
#ifdef __cplusplus
extern "C" {
#endif

enum {
    RIDE_KIND_NORMAL = 0, RIDE_KIND_KEYWORD, RIDE_KIND_TYPE, RIDE_KIND_STRING,
    RIDE_KIND_CHAR, RIDE_KIND_COMMENT, RIDE_KIND_PREPROC, RIDE_KIND_NUMBER,
    RIDE_KIND_LABEL
};
enum { RIDE_LANG_PLAIN = 0, RIDE_LANG_C, RIDE_LANG_CPP, RIDE_LANG_SHALIMAR,
       RIDE_LANG_ASM, RIDE_LANG_JSON };

enum { RIDE_DIALECT_C = 0, RIDE_DIALECT_SHALIMAR };
enum { RIDE_TOOL_AUTO = 0, RIDE_TOOL_CC1, RIDE_TOOL_MSVC, RIDE_TOOL_SHC, RIDE_TOOL_CXX,
       RIDE_TOOL_CXX1 };
enum { RIDE_CONFIG_DEBUG = 0, RIDE_CONFIG_RELEASE };

void ride_watch_for_faults(const char* logPath);

char* ride_reindent(const char* text, int width, int tabs, int caseIndent,
                   int dialect);

char* ride_indent_after_newline(const char* text, int row, int col,
                               int width, int tabs, int caseIndent, int dialect);

char* ride_indent_for(const char* text, int row, int width, int tabs, int caseIndent,
                     int dialect);

void ride_free(char* what);

int ride_find_next(const char* text, const char* needle, int row, int col,
                  int* foundRow, int* foundCol);
int ride_find_previous(const char* text, const char* needle, int row, int col,
                      int* foundRow, int* foundCol);

char* ride_replace_all(const char* text, const char* needle, const char* with,
                      int* howMany);

const char* ride_settings_set_aside(void);

const char* ride_code_font(void);
int ride_remember_code_font(const char* described);

/* Win32 only: a Rich Edit's HWND. Elsewhere these do nothing. */
void ride_undo_suspend(void* windowHandle);
void ride_undo_resume(void* windowHandle);

int ride_language_for(const char* path);

int ride_dialect_for(int language);

int ride_highlight(const char* line, int language, int* state,
                  unsigned char* kinds, int kindsSize);

char* ride_describe_build(const char* assembly);

char* ride_debug_note(int kind, const char* arch);

typedef struct RIDEProject RIDEProject;

RIDEProject* ride_project_new(void);
void ride_project_free(RIDEProject* project);

/* A project's directory or its .pro. On failure the project is left not loaded; error says why,
   or is empty when there was no .pro there at all. */
int ride_project_load(RIDEProject* project, const char* directory,
                     char* error, int errorSize);

const char* ride_project_name(RIDEProject* project);
int ride_project_groups(RIDEProject* project);
const char* ride_project_group_name(RIDEProject* project, int group);
int ride_project_files(RIDEProject* project, int group);
const char* ride_project_file(RIDEProject* project, int group, int file);
const char* ride_project_absolute(RIDEProject* project, const char* relative);

int ride_project_indent_width(RIDEProject* project);
int ride_project_indent_tabs(RIDEProject* project);
int ride_project_case_indent(RIDEProject* project);
int ride_project_toolchain(RIDEProject* project);

int ride_configuration(void);
void ride_remember_configuration(int config);
const char* ride_project_arch(RIDEProject* project);
/* **A CCS project opened as it is** (src/ccs/ccsproject.h), when settings.json's "ccs" switch is
   on: ride_project_load takes its folder. Nothing of CCS's is written; the options dialog shows
   what was read and commits nothing; the report is the Messages line naming what RIDE cannot honour
   (lines '\n' apart, the first for Messages), the mapping the dialog's text, and the
   configuration the one settings.json remembers for it (-1 unsaid), which the window keeps there. */
int ride_project_is_ccs(RIDEProject* project);
/* **A CCS workspace** (src/ccs/ccsworkspace.h) is a folder of projects and never one: a window asks
   which, then opens <workspace>/<project>.pro, which holds the workspace and the project and nothing
   else. is_workspace says whether a folder is one; workspace_projects names its CCS projects, one per
   line ("" for none), valid until the next call; workspace_pro writes that .pro unless it is there and
   puts its path in file - 0, with the reason in error, when it cannot. */
int ride_ccs_is_workspace(const char* directory);
const char* ride_ccs_workspace_projects(const char* directory);
int ride_ccs_workspace_pro(const char* workspace, const char* project,
                           char* file, int fileSize, char* error, int errorSize);
const char* ride_project_ccs_report(RIDEProject* project, int config);
const char* ride_project_ccs_mapping(RIDEProject* project, int config);
int ride_project_ccs_configuration(RIDEProject* project);
void ride_project_remember_configuration(RIDEProject* project, int config);
/* Run file on one source of a several-source build: the count of sources it
   is one of, or 0 - then the window runs the project instead. */
int ride_project_runs_as_project(RIDEProject* project, const char* source);
// The project's own target and compiler, set from the Target and Tools
// menus while it is open and written to its .pro at once; 0 with no project.
int ride_project_set_arch(RIDEProject* project, const char* arch);
int ride_project_set_toolchain(RIDEProject* project, int kind);

int ride_project_allows(const char* relative, char* why, int whySize);

const char* ride_group_for_file(const char* name);

const char* ride_project_suffix(void);
// The product's name, from product.h: the window's title, its message boxes, its logs.
const char* ride_product_name(void);
const char* ride_version(void);

// A project's header directories and libraries, as one ';'-separated line
// each, relative to the root as the file has them; setting one saves.
const char* ride_project_includes(RIDEProject* project);
/* Build > Clean: what the open project's build made, and this process's scratch, removed - one full
   path a line, "" when there was nothing to remove. Sources, the .pro and CCS's files stay. */
const char* ride_project_clean(RIDEProject* project);
/* The header a line or a selection names - "Quat.h" from #include "Quat.h", "cstdio" from
   #include <cstdio>, or a selection that is itself a file name - or "" when it names none. */
const char* ride_header_named(const char* text);
/* That header found as a compiler looks for it - beside the file it is named from, the project's
   folder and include paths, the shared include paths, the installation's include and lib - by its
   full path, or "" where none has it. */
const char* ride_find_header(RIDEProject* project, const char* fromFile, const char* name);
const char* ride_project_libraries(RIDEProject* project);
int ride_project_set_includes(RIDEProject* project, const char* line);
int ride_project_set_libraries(RIDEProject* project, const char* line);

// **Compiler Options** (src/options.h): the table both windows draw their tabbed dialog from, and a
// draft the dialog edits - begun from the open project's options, or the installation's where none
// is open; commit writes the draft to the .pro or settings.json, and a dialog cancelled just drops it.
enum { RIDE_OPTION_CHECK = 0, RIDE_OPTION_CHOICE, RIDE_OPTION_TEXT };
int ride_option_count(void);
const char* ride_option_id(int index);
const char* ride_option_tab(int index);
const char* ride_option_label(int index);
int ride_option_control(int index);
const char* ride_option_choices(int index);   // a choice's values, '|' between them
const char* ride_option_hint(int index);
int ride_option_tab_count(void);
const char* ride_option_tab_name(int tab);
void ride_options_begin(RIDEProject* project);
const char* ride_options_value(RIDEProject* project, int config, const char* id);
void ride_options_set(RIDEProject* project, int config, const char* id, const char* value);
void ride_options_reset(RIDEProject* project, int config);
// Whether the option means anything for this target; ride_options_why says why not.
int ride_options_available(const char* id, const char* arch);
const char* ride_options_why(const char* id, const char* arch);
// The compiler and flags a tab's compiler is given for this configuration and target, from the draft.
const char* ride_options_preview(RIDEProject* project, int config, int tab, const char* arch);
int ride_options_commit(RIDEProject* project);

// The installation's settings.json, above bin/: where cxx1's headers (include) and cc1's (lib) are, and the vcvars64.bat named there if any.
const char* ride_install_file(void);
// Writes that file with the installer's defaults where there is none - ~/.ride/settings.json, from the installation's; 0 only when it is missing and cannot be written.
int ride_write_install_file_if_absent(void);
// The installation's include directories and libraries, one ';'-separated
// line each as settings.json has them; setting one writes the file.
const char* ride_includes(void);
const char* ride_libraries(void);
int ride_set_includes(const char* line);
/* The window's way of asking the build's question - "the project's own
   masm/link/lnk6x did not build it; use the native tools instead?" - a yes
   builds again through them (compile.h: setAskNative). Installed once, at
   start; the function is called on whichever thread is building. */
void ride_ask_native(int (*ask)(const char* question));
int ride_set_libraries(const char* line);
const char* ride_include_dir(void);
const char* ride_lib_dir(void);
int ride_remember_header_dirs(const char* include, const char* lib);
const char* ride_vcvars(void);
// The default compiler in settings.json, as an RIDE_TOOL_* number, and
// the choice written back when one is made from the menu.
int ride_default_compiler(void);
int ride_default_indent_width(void);
int ride_default_indent_tabs(void);
int ride_remember_default_compiler(int kind);
int ride_remember_vcvars(const char* file);
// The assembler named for x86_64-windows, and the choice written back.
const char* ride_assembler(void);
int ride_remember_assembler(const char* file);
// TI's C6000 compiler directory and the runtime directory beside it, for a
// tms6747 build's .out, and the choice written back.
// The linkers named for x86_64-windows and tms6747, and the choices written back.
const char* ride_linker(void);
int ride_remember_linker(const char* file);
const char* ride_tilinker(void);
int ride_remember_tilinker(const char* file);
const char* ride_ti(void);
const char* ride_tilib(void);
int ride_remember_ti(const char* dir, const char* lib);

int ride_project_save_as(RIDEProject* project, const char* file,
                            char* why, int whySize);

int ride_project_loaded(RIDEProject* project);
const char* ride_project_root(RIDEProject* project);
void ride_project_set_root(RIDEProject* project, const char* path);

void ride_project_close(RIDEProject* project);
const char* ride_project_relative(RIDEProject* project, const char* path);

// kind is the compiler chosen (RIDE_TOOL_*): a name with no extension
// gets that compiler's, or with the choice on automatic the project's usual.
int ride_create_file(RIDEProject* project, const char* relative, const char* group, int kind);
int ride_rename_file(RIDEProject* project, const char* fromAbsolute, const char* toRelative);
int ride_delete_file(RIDEProject* project, const char* absolute);
int ride_move_to_group(RIDEProject* project, const char* absolute, const char* group);
int ride_add_existing(RIDEProject* project, const char* absolute, const char* group);
// A file just saved under the project's root joins it; 0 when that did not apply, and nothing to say then.
int ride_adopt_saved(RIDEProject* project, const char* absolute);
// Whether the project lists this file, in any group.
int ride_project_holds(RIDEProject* project, const char* absolute);
// The file the project opens with, relative to its root - its own choice,
// else the one defining main, else the first - and remembering the one in
// front when the project is closed or the window left.
const char* ride_project_file_to_open(RIDEProject* project);
int ride_remember_open(RIDEProject* project, const char* absolute);

int ride_remove_from_project(RIDEProject* project, const char* absolute);

int ride_begin_from_what_is_there(RIDEProject* project, const char* directory);
const char* ride_last_project(void);
int ride_remember_project(const char* directory);
// The last three projects opened, most recent first; empty past the end.
const char* ride_recent_project(int index);
const char* ride_recent_file(int index);
int ride_remember_file(const char* path);
const char* ride_demo_directory(void);

int ride_begin_project(RIDEProject* project, const char* directory, const char* name,
                      const char* firstFile);
int ride_save_project(RIDEProject* project);

const char* ride_outcome_message(RIDEProject* project);
const char* ride_outcome_path(RIDEProject* project);

/* One diagnostic read from compiler output - one line, or two for cc1's preprocessor - by the core's
   own parser; 1 when there is one. *file and *message hold until the next call. Any one thread. */
int ride_parse_diagnostic(const char* text, const char* source, int* line, int* column,
                          const char** file, const char** message);
/* The three compilers' names as people type them; their programs are these with .exe. */
enum { RIDE_COMPILER_C = 0, RIDE_COMPILER_CPP, RIDE_COMPILER_SHALIMAR };
const char* ride_compiler_name(int which);

const char* ride_arch(int index);
int ride_arch_count(void);
const char* ride_toolchain_name(int kind);

const char* ride_language_name(int language);
const char* ride_config_name(int config);
int ride_resolve(int toolchainKind, int language);
int ride_can_compile(int kind, int language);
const char* ride_refusal(int kind, int language);
int ride_uses_arch(int kind);

int ride_runs_here(int kind, const char* arch);
const char* ride_why_not_run(int kind, const char* arch);
const char* ride_host_arch(void);

const char* ride_shown_command(RIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind,
                              const char* source, int language, const char* arch,
                              int config);

int ride_converts_from(int language, int* toShalimar);
char* ride_find_converter(void);
char* ride_converted_name(const char* source, int toShalimar);

typedef struct RIDEConversion RIDEConversion;

RIDEConversion* ride_convert(const char* converter, const char* source,
                                   const char* output, int toShalimar);
void ride_conversion_free(RIDEConversion* made);
int ride_conversion_ran(RIDEConversion* made);
int ride_conversion_ok(RIDEConversion* made);
const char* ride_conversion_produced(RIDEConversion* made);
const char* ride_conversion_output(RIDEConversion* made);

typedef struct RIDEBuild RIDEBuild;

RIDEBuild* ride_build(RIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind, const char* source,
                    int language, const char* arch, int config);

int ride_project_builds(RIDEProject* project);
/* Works out what the project builds; the ride_project_target_* and _part_* answers below are its,
   until it is called again. ride_build_target and ride_project_debug_plan call it themselves. */
int ride_project_target_ready(RIDEProject* project);
const char* ride_project_target_why(RIDEProject* project);
const char* ride_project_target_detail(RIDEProject* project);
int ride_project_target_language(RIDEProject* project);
int ride_project_target_sources(RIDEProject* project);

int ride_project_target_parts(RIDEProject* project);
const char* ride_project_part_group(RIDEProject* project, int index);
int ride_project_part_language(RIDEProject* project, int index);
int ride_project_part_toolchain(RIDEProject* project, int index, const char* cc1,
                               const char* cl, const char* shc, const char* cxx1, int kind);
const char* ride_project_target_source(RIDEProject* project, int index);
const char* ride_project_target_program(RIDEProject* project);

int ride_project_debug_plan(RIDEProject* project, const char* cc1, const char* cl,
                           const char* shc, const char* cxx1, int kind, const char* arch);
int ride_project_debug_kind(RIDEProject* project);
const char* ride_project_why_not_debug(RIDEProject* project);
int ride_project_blind_groups(RIDEProject* project);
const char* ride_project_blind_group(RIDEProject* project, int index);

RIDEBuild* ride_build_target(RIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1,
                           int kind, const char* arch, int config);
void ride_build_free(RIDEBuild* built);

int ride_build_ok(RIDEBuild* built);
/* What the build made and what runs: the program, or <program>.vm for the emulated target; and
   whether it is Shalimar, which the emulator runs beside its runtime. Run it with ride_run_made_start. */
const char* ride_build_made(RIDEBuild* built);
int ride_build_made_shalimar(RIDEBuild* built);
const char* ride_build_output(RIDEBuild* built);
const char* ride_build_assembly(RIDEBuild* built);
int ride_build_assembly_lines(RIDEBuild* built);
int ride_build_has_error(RIDEBuild* built);

const char* ride_build_error_file(RIDEBuild* built);
int ride_build_error_line(RIDEBuild* built);
int ride_build_error_column(RIDEBuild* built);
const char* ride_build_error_message(RIDEBuild* built);

typedef struct RIDERan RIDERan;

RIDERan* ride_run(RIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind, const char* source,
                int language, const char* arch, int config);

RIDERan* ride_run_built(const char* program);
void ride_run_free(RIDERan* ran);

int ride_ran_built(RIDERan* ran);
int ride_ran_ran(RIDERan* ran);
int ride_ran_status(RIDERan* ran);
const char* ride_ran_output(RIDERan* ran);
int ride_ran_has_error(RIDERan* ran);
int ride_ran_error_line(RIDERan* ran);
int ride_ran_error_column(RIDERan* ran);
const char* ride_ran_error_message(RIDERan* ran);
/* The file the error is in - a header, perhaps, and not the source that was built. */
const char* ride_ran_error_file(RIDERan* ran);

/* ---- a program that runs while the window watches (README.md, "Input, and Stop") -------------
   RIDEOutput runs on a worker thread: marshal to the window's own and never wait there for a thread
   that may call ride_running_free. Bytes are not kept after the call; bytes NULL means it is over. */
/* The program's terminal echoes what is sent, so the window does not. On Windows it is a pseudo-
   console, whose one output carries stderr as RIDE_STREAM_OUT; before 1809, pipes, and no echo. */
enum { RIDE_STREAM_OUT = 0, RIDE_STREAM_ERR = 1, RIDE_STREAM_BUILD = 2 };
typedef void (*RIDEOutput)(void* user, const char* bytes, int size, int stream);

typedef struct RIDERunning RIDERunning;

/* ride_run's build, its lines as RIDE_STREAM_BUILD, then the program with a real input; the program
   is removed afterwards. The project is read here, on this thread, and not again. */
RIDERunning* ride_run_start(RIDEProject* project, const char* cc1, const char* cl, const char* shc,
                            const char* cxx1, int kind, const char* source, int language,
                            const char* arch, int config, RIDEOutput onOutput, void* user);
/* A program already built, as ride_run_built runs it; it is left where it is. */
RIDERunning* ride_run_built_start(const char* program, RIDEOutput onOutput, void* user);
/* The same for what ride_build_made named, Shalimar or not. */
RIDERunning* ride_run_made_start(const char* program, int shalimar, RIDEOutput onOutput, void* user);
/* The same two, handing the program a command line - Build > Command-line arguments - split as a shell
   would at spaces outside quotes; a word naming a file under `base` (the project, or the file's folder)
   goes by its full name. */
RIDERunning* ride_run_made_start_line(const char* program, int shalimar, const char* line, const char* base,
                                      RIDEOutput onOutput, void* user);
RIDERunning* ride_run_start_line(RIDEProject* project, const char* cc1, const char* cl, const char* shc,
                                 const char* cxx1, int kind, const char* source, int language,
                                 const char* arch, int config, const char* line, const char* base,
                                 RIDEOutput onOutput, void* user);
/* The project's command-line arguments as one line, and setting them from one: kept in its .pro, or for a
   CCS project in RIDE's own state. The line said before a run: "$ program arguments". */
char* ride_project_arguments(RIDEProject* project);
int ride_project_set_arguments(RIDEProject* project, const char* line);
char* ride_project_run_line(RIDEProject* project, const char* program);
/* "$ program line", the program by its name in the project where it is in one. */
char* ride_run_line(RIDEProject* project, const char* program, const char* line);

/* **What a ride_*_start runs, for tms6747 (R5).** Said just before the build, and taken by the start:
   RIDE_RUN_PROGRAM on tms6747 is the build's linked <program>.out on sim6747, as RIDE_RUN_SIMULATOR
   says outright - or vm6747's when the .pro says emulateOnly; RIDE_RUN_EMULATOR (Build > Emulate on
   vm6747) runs the assembly on vm6747 and the build links no .out; RIDE_RUN_VERIFY runs both, captured -
   status 0 they agree, 1 they differ, 2 one could not run. Why not is said on the output when it cannot. */
enum { RIDE_RUN_PROGRAM = 0, RIDE_RUN_SIMULATOR = 1, RIDE_RUN_VERIFY = 2, RIDE_RUN_EMULATOR = 3 };
void ride_run_next(int runner);
/* 1 when sim6747 is beside the editor (or named by $SIM6747). */
int ride_simulator_here(void);

/* Bytes to its input as they are - a line wants its "\n"; held until it starts. 0 once it has
   ended or its input was closed. Any thread, as are the next three. */
int ride_running_send(RIDERunning* running, const char* bytes, int size);
/* End of input: its next read sees end of file. */
void ride_running_close_input(RIDERunning* running);
/* Ends the build, or the program and all it started; the run ends as usual, stopped. */
void ride_running_stop(RIDERunning* running);
/* Up to timeoutMs (negative: for ever) for the run to end; 1 when it has. */
int ride_running_wait(RIDERunning* running, int timeoutMs);

/* Answered once done is 1, 0 or "" before; the strings live as long as the RIDERunning. */
int ride_running_done(RIDERunning* running);
int ride_running_built(RIDERunning* running);   /* there was a program to run */
int ride_running_ran(RIDERunning* running);     /* and it started */
int ride_running_status(RIDERunning* running);  /* exit code, or 128 + the signal */
int ride_running_stopped(RIDERunning* running); /* Stop ended it, or its build */
const char* ride_running_build_output(RIDERunning* running);
int ride_running_has_error(RIDERunning* running);
const char* ride_running_error_file(RIDERunning* running);
int ride_running_error_line(RIDERunning* running);
int ride_running_error_column(RIDERunning* running);
const char* ride_running_error_message(RIDERunning* running);

/* Stops a run still going, joins its worker, lets it go. Not from inside RIDEOutput. */
void ride_running_free(RIDERunning* running);

/* Stop for a build, any thread: every compiler, linker and converter the calls here are running is
   killed, and each call returns failed with "[stopped]" in its output and its _stopped answer 1.
   A blocking ride_run or ride_run_built has its program ended the same way. */
void ride_cancel_builds(void);
int ride_build_stopped(RIDEBuild* built);
int ride_ran_stopped(RIDERan* ran);
int ride_conversion_stopped(RIDEConversion* made);

const char* ride_shown_run_command(RIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind,
                                  const char* source, int language, const char* arch,
                                  int config);

char* ride_about(void);
/* Edit > Find in Files: a heading line, then a line a hit - "<file>:<line>:<col>: <the line>", or for a
   name search "<file>" alone - at most 2000 of them. Freed by the caller. */
// Ends a Find in Files running on another thread at its next file; its answer then says "stopped".
void ride_find_stop(void);
char* ride_find_in_files(const char* text, const char* folder, const char* patterns,
                         int matchCase, int wholeWord, int subfolders, int namesOnly);
// About without its last lines, and those lines alone: the macOS panel puts them in its line under the credits.
char* ride_about_credits(void);
char* ride_about_stamp(void);
/* Help > Environment: every tool, header directory and library in force, and where each came from. */
char* ride_environment(void);

typedef struct RIDEProgram RIDEProgram;

RIDEProgram* ride_build_program(RIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind, const char* source,
                              int language, const char* arch, int config);
/* Deletes the built program from the disk, and what was built beside it, then lets it go. */
void ride_program_free(RIDEProgram* built);

int ride_program_ok(RIDEProgram* built);
const char* ride_program_path(RIDEProgram* built);
const char* ride_program_output(RIDEProgram* built);
int ride_program_has_error(RIDEProgram* built);
int ride_program_error_line(RIDEProgram* built);
int ride_program_error_column(RIDEProgram* built);
const char* ride_program_error_message(RIDEProgram* built);
const char* ride_program_error_file(RIDEProgram* built);

int ride_debugger_for(int kind, const char* arch);
const char* ride_debugger_name(int kind);
const char* ride_no_debugger_because(int kind, const char* arch);

int ride_debugger_stops_itself(int kind);

const char* ride_release_cannot_stop(int kind);

const char* ride_why_it_did_not_start(int kind, const char* arch);

typedef struct RIDEDebugger RIDEDebugger;

RIDEDebugger* ride_debugger_new(void);
void ride_debugger_free(RIDEDebugger* debugger);

int ride_debugger_start(RIDEDebugger* debugger, int kind, const char* arch,
                       const char* program);
int ride_debugger_running(RIDEDebugger* debugger);
void ride_debugger_stop(RIDEDebugger* debugger);

int ride_debugging_shalimar(RIDEDebugger* debugger);

const char* ride_locals_none_because(RIDEDebugger* debugger);
const char* ride_cannot_watch(RIDEDebugger* debugger);
const char* ride_cannot_walk_stack(RIDEDebugger* debugger);

int ride_debugger_break(RIDEDebugger* debugger, const char* file, int line);
int ride_debugger_clear(RIDEDebugger* debugger);

void ride_debugger_run(RIDEDebugger* debugger);
void ride_debugger_resume(RIDEDebugger* debugger);
void ride_debugger_step_over(RIDEDebugger* debugger);
void ride_debugger_step_into(RIDEDebugger* debugger);
void ride_debugger_step_out(RIDEDebugger* debugger);

int ride_stop_stopped(RIDEDebugger* debugger);
int ride_stop_exited(RIDEDebugger* debugger);
int ride_stop_status(RIDEDebugger* debugger);
const char* ride_stop_file(RIDEDebugger* debugger);
int ride_stop_line(RIDEDebugger* debugger);
const char* ride_stop_function(RIDEDebugger* debugger);

const char* ride_stop_said(RIDEDebugger* debugger);

const char* ride_stop_output(RIDEDebugger* debugger);

int ride_stop_no_source(RIDEDebugger* debugger);

int ride_locals_count(RIDEDebugger* debugger);
const char* ride_local_name(RIDEDebugger* debugger, int index);
const char* ride_local_type(RIDEDebugger* debugger, int index);
const char* ride_local_value(RIDEDebugger* debugger, int index);
// Where it lives, "0x...", or "" in a register or for Shalimar - the grids' Address column.
const char* ride_local_address(RIDEDebugger* debugger, int index);

const char* ride_local_text(RIDEDebugger* debugger, int index);
int ride_locals_on_line(RIDEDebugger* debugger, const char* line);
int ride_set_variable(RIDEDebugger* debugger, const char* name, const char* value);
const char* ride_set_complaint(RIDEDebugger* debugger);

void ride_watch_add(RIDEDebugger* debugger, const char* expression);
int ride_watch_count(RIDEDebugger* debugger);
const char* ride_watch_text(RIDEDebugger* debugger, int index);
const char* ride_watch_expression(RIDEDebugger* debugger, int index);
// A watch's fields apart, for the grid: its value (or the refusal), 1 when it is a value, and its address.
const char* ride_watch_value(RIDEDebugger* debugger, int index);
int ride_watch_ok(RIDEDebugger* debugger, int index);
const char* ride_watch_address(RIDEDebugger* debugger, int index);
const char* ride_watch_type(RIDEDebugger* debugger, int index);
int ride_watch_on_line(RIDEDebugger* debugger, const char* line);
void ride_watch_set(RIDEDebugger* debugger, int index, const char* expression);

int ride_stack_count(RIDEDebugger* debugger);
const char* ride_stack_function(RIDEDebugger* debugger, int index);
const char* ride_stack_file(RIDEDebugger* debugger, int index);
int ride_stack_line(RIDEDebugger* debugger, int index);

const char* ride_stack_text(RIDEDebugger* debugger, int index);
int ride_stack_on_line(RIDEDebugger* debugger, const char* line);

int ride_debugger_look_at(RIDEDebugger* debugger, int which);

const char* ride_stop_line_text(const char* file, int line, const char* function);
int ride_looking_at(RIDEDebugger* debugger);
const char* ride_looking_text(RIDEDebugger* debugger);

#ifdef __cplusplus
}
#endif

#endif
