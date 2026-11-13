// =============================================================================
//  Block.h - the bridge of the C++/CLI outlooks (Black, Grey) to the block, compile.dll (common\compile,
//  common\Common_Compilation_Design.md).
//  The block plans a build - which tools, which command lines, in which order, and the build's log - and never
//  runs one. This header is managed only: Block.cpp is the one file of an outlook that includes gride_compile.h.
// =============================================================================
#pragma once

namespace GrideBlock {

    using namespace System;
    using namespace System::Collections::Generic;

    public ref class BlockStep
    {
    public:
        property String^ Kind;                  // compile | assemble | link
        property String^ Tool;                  // toolchain.json's id: c90, masm, link ...
        property String^ Program;
        property String^ CommandLine;           // the program and its arguments, quoted as Windows wants them
        property String^ Arguments;             // the arguments alone, quoted the same
        property String^ Directory;
        property List<String^>^ Environment;    // "NAME=value", this step's own
        property List<String^>^ Outputs;
        property String^ Shown;                 // what it is about: main.c
        property String^ Source;                // the file it reads first: a message that names no file is about this one
        property bool Utf8;                     // the tool writes UTF-8 (else the console's code page)
        property int Wave;                      // steps of one wave may run together
    };

    public ref class BlockProblem
    {
    public:
        property String^ Severity;              // error | warning | note
        property String^ File;
        property String^ Text;
    };

    // the block's actions Black asks for
    public enum class BlockAction { Build, Rebuild, Clean, CompileFile };

    // One planned build: its steps, its log and its stamps. Made by Block::Plan; holds the block's own records
    // until it is disposed of.
    public ref class BlockPlan
    {
    public:
        property List<BlockStep^>^ Steps;
        property List<BlockProblem^>^ Problems;
        property List<String^>^ Cleans;         // Clean and Rebuild: the folders to empty first
        property bool Runnable;                 // no error among the problems
        property bool AsksNative;
        property String^ Program;               // what the link writes, or ""
        property String^ ObjectFolder;
        property String^ OutputFolder;
        property String^ LogFile;

        void WriteFiles();                      // the files the plan has written for a tool (ti-link.cmd), and the folders

        // the build log (design 5.1): the lines now due, to be printed as they are
        List<String^>^ Begin();
        List<String^>^ Finished(int step, int exitCode, String^ output, double seconds);
        List<String^>^ UpToDate(int step);
        List<String^>^ End(double elapsedSeconds);
        String^ DryRun();                       // "DRY RUN ONLY", then every command of a full build, written whole

        // which steps need to run (design 5, "Up to date")
        bool IsUpToDate(int step);
        void Ran(int step, bool succeeded);
        void SaveStamps();

        ~BlockPlan() { this->!BlockPlan(); }
        !BlockPlan();
    internal:
        BlockPlan() { plan = nullptr; log = nullptr; stamps = nullptr; }
        void *plan;                             // gride::compile::Plan
        void *log;                              // gride::compile::BuildLog
        void *stamps;                           // gride::compile::Stamps
    };

    public ref class Block abstract sealed
    {
    public:
        // why the block cannot be used - compile.dll is not beside GRIDE.exe, or is not the one this GRIDE.exe was
        // built with - or null. Asked before anything else: nothing of the DLL is touched while this is not null.
        static property String^ Problem { String^ get(); }
        static property String^ Version { String^ get(); }      // what the DLL beside GRIDE.exe says, or "" when it cannot be used
        static property String^ Expected { String^ get(); }     // the one this GRIDE.exe was built with: "compile 1.0 - plans GRIDE's build commands"

        // a plan for one project; `outlook` is the head's last word ("Black 1.5.0"), `arch` toolchain.json's
        static BlockPlan^ Plan(String^ project, String^ configuration, String^ arch, BlockAction action, String^ file,
                               bool native, String^ workspaceFolder, bool showCommands, String^ outlook);
    private:
        static String^ problem;
        static bool looked;
    };
}
