// =============================================================================
//  CodeConverter.h - C <-> Shalimar source conversion (Tools > Convert)
//  Runs toolchain.json's converter (the tool of role "converter", c2s) with its
//  convertToShalimar / convertToC operation, found as its lookup says. A tool or a
//  command template named in Tools > Options > Converters takes its place.
// =============================================================================
#pragma once
#include "MacroExpander.h"
#include "ToolRunner.h"
#include "../Settings/GrideSettings.h"

namespace Gride { namespace Services {

    using namespace Gride::Core;
    using namespace Gride::Settings;

    public enum class ConvertDirection { CToShalimar, ShalimarToC };

    public ref class CodeConverter
    {
    public:
        CodeConverter(ConverterSettings^ s)
        {
            settings = s;
            log = gcnew StringBuilder();
        }

        property String^ Log { String^ get() { return log->ToString(); } }

        // toolchain.json's converter (c2s), or null when the file names none
        static ToolDefinition^ Definition()
        {
            return ToolchainDefinitions::Current->ToolByRole("converter");
        }

        // the operation of toolchain.json's converter for a direction
        static String^ OperationId(ConvertDirection d)
        {
            return d == ConvertDirection::CToShalimar ? "convertToShalimar" : "convertToC";
        }

        // the converter for a direction: the one named in Options > Converters, else toolchain.json's
        // converter where its lookup finds it, else its bare name, so a "not found" message names it
        String^ ToolPath(ConvertDirection d)
        {
            String^ named = Named(d);
            if (named->Length > 0) return Environment::ExpandEnvironmentVariables(named);
            ToolDefinition^ t = Definition();
            if (t == nullptr) return String::Empty;
            String^ found = RideToolchain::FindTool(t, String::Empty, ToolchainSettings::RideRoot);
            return found != nullptr ? found : t->Exe;
        }

        // where toolchain.json's converter was looked for (empty when Options names the tool)
        String^ LookedIn(ConvertDirection d)
        {
            if (Named(d)->Length > 0) return String::Empty;
            ToolDefinition^ t = Definition();
            return t == nullptr ? "toolchain.json names no converter" : RideToolchain::SearchPlaces(t, String::Empty, ToolchainSettings::RideRoot);
        }

        // the command line: Options' template when one is set, else the converter's operation from
        // toolchain.json ("--to-shalimar" / "--to-c" for c2s), else GRIDE 1.0's "tool input -o output"
        String^ CommandLine(String^ tool, String^ input, String^ output, ConvertDirection d)
        {
            MacroExpander^ mx = gcnew MacroExpander();
            mx->Set("Tool", tool);
            mx->Set("Input", input);
            mx->Set("Source", input);
            mx->Set("Output", output);
            String^ templ = settings->CommandTemplate == nullptr ? String::Empty : settings->CommandTemplate->Trim();
            if (templ->Length == 0)
            {
                ToolDefinition^ t = Definition();
                String^ args = t == nullptr ? String::Empty : t->OperationArgs(OperationId(d));
                templ = args->Length > 0 ? "\"$(Tool)\" " + args : ConverterSettings::Gride1Template;
            }
            return mx->Expand(templ);
        }

        static ConvertDirection DirectionFor(String^ input)
        {
            return PathUtil::HasExtension(input, ".shl") || PathUtil::HasExtension(input, ".shm") ? ConvertDirection::ShalimarToC : ConvertDirection::CToShalimar;
        }

        // kalman.c -> kalman.shl ; if it exists: kalman_conv.shl
        static String^ SuggestOutput(String^ input, ConvertDirection d)
        {
            String^ ext = ".c";
            if (d == ConvertDirection::CToShalimar) ext = ".shl";
            String^ outPath = Path::ChangeExtension(input, ext);
            if (File::Exists(outPath))
                outPath = Path::Combine(Path::GetDirectoryName(input), Path::GetFileNameWithoutExtension(input) + "_conv" + ext);
            return outPath;
        }

        // true when the converter wrote the output file in this run. c2s exits 1 both when it wrote the
        // file with BEYOND marks and when it refused (then it writes nothing), so a fresh file decides.
        bool Convert(String^ input, String^ output, ConvertDirection d)
        {
            log->Clear();
            String^ tool = ToolPath(d);
            if (String::IsNullOrEmpty(tool) || !File::Exists(tool))
            {
                log->AppendLine("Converter not found: " + (String::IsNullOrEmpty(tool) ? "(none)" : tool));
                String^ looked = LookedIn(d);
                if (looked->Length > 0) log->AppendLine("Looked in: " + looked);
                log->AppendLine("Put c2s.exe beside GRIDE.exe, or set the converter in Tools > Options > Converters.");
                return false;
            }
            String^ cmd = CommandLine(tool, input, output, d);
            log->AppendLine("> " + cmd);

            DateTime before = File::Exists(output) ? File::GetLastWriteTimeUtc(output) : DateTime::MinValue;
            ToolRunner^ r = gcnew ToolRunner();
            r->Line += gcnew TextLineHandler(this, &CodeConverter::OnLine);
            int code = r->Run(cmd, Path::GetDirectoryName(input));
            bool written = File::Exists(output) && File::GetLastWriteTimeUtc(output) > before;
            log->AppendLine(String::Format("Converter exited with code {0}", code));
            if (written && code != 0) log->AppendLine("Written with marks to review (BEYOND) - see the lines above.");
            if (!written) log->AppendLine("No output was written.");
            return written && (code == 0 || code == 1);
        }

    private:
        ConverterSettings^ settings;
        StringBuilder^ log;
        void OnLine(String^ line) { log->AppendLine(line); }

        String^ Named(ConvertDirection d)
        {
            String^ t = d == ConvertDirection::CToShalimar ? settings->CToShalimar : settings->ShalimarToC;
            return t == nullptr ? String::Empty : t->Trim();
        }
    };
}}
