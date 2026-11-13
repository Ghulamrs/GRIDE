// =============================================================================
//  Block.cpp - the bridge of the C++/CLI outlooks (Black, Grey) to compile.dll: the one file of an outlook that
//  includes the block's header. Each outlook's project compiles this file and delay-loads the DLL, so the outlook
//  starts without it; Block::Problem says why the block cannot be used, and nothing below touches the DLL while
//  it does.
// =============================================================================
#include <string>
#include <vector>

#include "../gride_compile.h"

#include "Block.h"

using namespace System;
using namespace System::Collections::Generic;
using namespace System::IO;

namespace GrideBlock {

    namespace bc = gride::compile;

    namespace {

        std::string Narrow(String^ s)
        {
            if (String::IsNullOrEmpty(s)) return std::string();
            array<Byte>^ bytes = System::Text::Encoding::UTF8->GetBytes(s);
            if (bytes->Length == 0) return std::string();
            pin_ptr<Byte> p = &bytes[0];
            return std::string(reinterpret_cast<const char *>(p), static_cast<size_t>(bytes->Length));
        }

        String^ Wide(const std::string &s)
        {
            if (s.empty()) return String::Empty;
            array<Byte>^ bytes = gcnew array<Byte>(static_cast<int>(s.size()));
            System::Runtime::InteropServices::Marshal::Copy(IntPtr(const_cast<char *>(s.data())), bytes, 0, bytes->Length);
            return System::Text::Encoding::UTF8->GetString(bytes);
        }

        List<String^>^ Lines(const std::vector<std::string> &lines)
        {
            List<String^>^ r = gcnew List<String^>();
            for (size_t i = 0; i < lines.size(); ++i) r->Add(Wide(lines[i]));
            return r;
        }

        String^ AppFolder()
        {
            try { return Path::GetDirectoryName(System::Reflection::Assembly::GetEntryAssembly()->Location); }
            catch (Exception^) { return AppDomain::CurrentDomain->BaseDirectory; }
        }

        // the DLL's own word for itself; in a function of its own so that a DLL that cannot be loaded fails here
        String^ DllVersion() { return Wide(bc::version()); }
    }

    String^ Block::Problem::get()
    {
        if (looked) return problem;
        looked = true;
        problem = nullptr;
        String^ dll = Path::Combine(AppFolder(), "compile.dll");
        if (!File::Exists(dll)) { problem = "compile.dll is not beside GRIDE.exe (" + dll + ")"; return problem; }
        try
        {
            String^ theirs = DllVersion();
            if (theirs != GRIDE_COMPILE_VERSION)
                problem = "compile.dll is not the one this GRIDE.exe was built with: it says \"" + theirs + "\", and \"" GRIDE_COMPILE_VERSION "\" is wanted";
        }
        catch (Exception^ ex) { problem = "compile.dll could not be loaded: " + ex->Message; }
        return problem;
    }

    String^ Block::Version::get() { return Problem == nullptr ? DllVersion() : String::Empty; }

    // from the header this GRIDE.exe was compiled against, not from the DLL: nothing of the DLL is touched
    String^ Block::Expected::get() { return GRIDE_COMPILE_VERSION; }

    BlockPlan^ Block::Plan(String^ project, String^ configuration, String^ arch, BlockAction action, String^ file,
                           bool native, String^ workspaceFolder, bool showCommands, String^ outlook)
    {
        if (Problem != nullptr) return nullptr;
        // whose old settings keys are read first: the outlook's own - the head's word before the number
        String^ whose = String::IsNullOrEmpty(outlook) ? String::Empty : outlook->Split(L' ')[0];
        bc::Paths paths = bc::defaultPaths(Narrow(AppFolder()), Narrow(whose));
        bc::Workspace workspace = bc::Workspace::load(paths, bc::systemFileSystem(), bc::systemEnvironment());
        bc::Request request;
        request.project = Narrow(project);
        request.configuration = Narrow(configuration);
        request.target = Narrow(arch);
        request.action = action == BlockAction::Rebuild ? bc::Rebuild : action == BlockAction::Clean ? bc::Clean
                       : action == BlockAction::CompileFile ? bc::CompileFile : bc::Build;
        request.file = Narrow(file);
        request.native = native;
        request.workspaceFolder = Narrow(workspaceFolder);

        bc::Plan *plan = new bc::Plan(workspace.plan(request));
        BlockPlan^ r = gcnew BlockPlan();
        r->plan = plan;
        bc::BuildLog *log = new bc::BuildLog(workspace, request, *plan, Narrow(outlook), Narrow(DateTime::Now.ToString("dd-MM-yyyy HH:mm:ss")));
        log->showCommands(showCommands);
        r->log = log;
        r->stamps = new bc::Stamps(workspace, request, *plan);

        r->Steps = gcnew List<BlockStep^>();
        for (size_t i = 0; i < plan->steps.size(); ++i)
        {
            const bc::Step &s = plan->steps[i];
            BlockStep^ b = gcnew BlockStep();
            b->Kind = Wide(s.kind);
            b->Tool = Wide(s.tool);
            b->Program = Wide(s.program);
            b->CommandLine = Wide(bc::windowsCommandLine(s.program, s.argv));
            {
                // the arguments alone, for a runner that takes the program and its arguments apart
                std::string whole = bc::windowsCommandLine(s.program, s.argv), alone = bc::windowsCommandLine(s.program, std::vector<std::string>());
                b->Arguments = Wide(whole.size() > alone.size() ? whole.substr(alone.size() + 1) : std::string());
            }
            b->Directory = Wide(s.directory);
            b->Environment = gcnew List<String^>();
            for (size_t k = 0; k < s.environment.size(); ++k) b->Environment->Add(Wide(s.environment[k].first + "=" + s.environment[k].second));
            b->Outputs = Lines(s.outputs);
            b->Shown = Wide(s.shown);
            b->Source = s.inputs.empty() ? String::Empty : Wide(s.inputs[0]);
            b->Utf8 = s.encoding == "utf-8";
            b->Wave = s.wave;
            r->Steps->Add(b);
        }
        r->Problems = gcnew List<BlockProblem^>();
        for (size_t i = 0; i < plan->problems.size(); ++i)
        {
            BlockProblem^ p = gcnew BlockProblem();
            p->Severity = Wide(plan->problems[i].severity);
            p->File = Wide(plan->problems[i].file);
            p->Text = Wide(plan->problems[i].text);
            r->Problems->Add(p);
        }
        r->Cleans = Lines(plan->cleans);
        r->Runnable = plan->runnable();
        r->AsksNative = plan->asksNative;
        r->Program = Wide(plan->program);
        r->ObjectFolder = Wide(plan->objectFolder);
        r->OutputFolder = Wide(plan->outputFolder);
        r->LogFile = Wide(log->file());
        return r;
    }

    BlockPlan::!BlockPlan()
    {
        delete static_cast<bc::Stamps *>(stamps); stamps = nullptr;
        delete static_cast<bc::BuildLog *>(log); log = nullptr;
        delete static_cast<bc::Plan *>(plan); plan = nullptr;
    }

    void BlockPlan::WriteFiles()
    {
        const bc::Plan &p = *static_cast<bc::Plan *>(plan);
        try
        {
            if (!String::IsNullOrEmpty(OutputFolder)) System::IO::Directory::CreateDirectory(OutputFolder);
            if (!String::IsNullOrEmpty(ObjectFolder)) System::IO::Directory::CreateDirectory(ObjectFolder);
        }
        catch (Exception^) { }     // a folder that cannot be made: the first tool to write there says so
        for (size_t i = 0; i < p.files.size(); ++i) bc::systemFileSystem().write(p.files[i].first, p.files[i].second);
    }

    List<String^>^ BlockPlan::Begin() { return Lines(static_cast<bc::BuildLog *>(log)->begin()); }

    List<String^>^ BlockPlan::Finished(int step, int exitCode, String^ output, double seconds)
    {
        return Lines(static_cast<bc::BuildLog *>(log)->finished(step, exitCode, Narrow(output), seconds));
    }

    List<String^>^ BlockPlan::UpToDate(int step) { return Lines(static_cast<bc::BuildLog *>(log)->upToDate(step)); }
    List<String^>^ BlockPlan::End(double elapsedSeconds) { return Lines(static_cast<bc::BuildLog *>(log)->end(elapsedSeconds)); }
    String^ BlockPlan::DryRun() { return Wide(static_cast<bc::BuildLog *>(log)->dryRun()); }

    bool BlockPlan::IsUpToDate(int step) { return static_cast<bc::Stamps *>(stamps)->upToDate(step); }
    void BlockPlan::Ran(int step, bool succeeded) { static_cast<bc::Stamps *>(stamps)->ran(step, succeeded); }
    void BlockPlan::SaveStamps() { static_cast<bc::Stamps *>(stamps)->save(); }
}
