// =============================================================================
//  CodeConverter.h - C <-> Shalimar source conversion (Tools > Convert)
//  Runs the converter tool configured in Tools > Options > Converters with the
//  template  "$(Tool)" "$(Input)" -o "$(Output)"
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

        String^ ToolPath(ConvertDirection d)
        {
            String^ t = d == ConvertDirection::CToShalimar ? settings->CToShalimar : settings->ShalimarToC;
            return Environment::ExpandEnvironmentVariables(t == nullptr ? String::Empty : t);
        }

        static ConvertDirection DirectionFor(String^ input)
        {
            return PathUtil::HasExtension(input, ".shl") ? ConvertDirection::ShalimarToC : ConvertDirection::CToShalimar;
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

        // returns true when the tool ran and produced the output file
        bool Convert(String^ input, String^ output, ConvertDirection d)
        {
            log->Clear();
            String^ tool = ToolPath(d);
            if (!File::Exists(tool))
            {
                log->AppendLine("Converter not found: " + tool);
                log->AppendLine("Set its path in Tools > Options > Converters.");
                return false;
            }
            MacroExpander^ mx = gcnew MacroExpander();
            mx->Set("Tool", tool);
            mx->Set("Input", input);
            mx->Set("Output", output);
            String^ cmd = mx->Expand(settings->CommandTemplate);
            log->AppendLine("> " + cmd);

            ToolRunner^ r = gcnew ToolRunner();
            r->Line += gcnew TextLineHandler(this, &CodeConverter::OnLine);
            int code = r->Run(cmd, Path::GetDirectoryName(input));
            log->AppendLine(String::Format("Converter exited with code {0}", code));
            return code == 0 && File::Exists(output);
        }

    private:
        ConverterSettings^ settings;
        StringBuilder^ log;
        void OnLine(String^ line) { log->AppendLine(line); }
    };
}}
