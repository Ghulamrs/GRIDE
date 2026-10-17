#include "convert.h"

#include <cstdlib>

#include "path.h"

namespace editor {

bool convertsFrom(Language lang, bool* toShalimar) {
    if (lang == LangC) { *toShalimar = true; return true; }
    if (lang == LangShalimar) { *toShalimar = false; return true; }
    return false;
}

std::string findConverter() {
    const char* fromEnv = std::getenv("C2S");
    if (fromEnv && *fromEnv) return fromEnv;

    std::string beside = path::besideProgram("c2s.exe");
    if (beside.empty()) beside = path::besideProgram("c2s");
    return beside;
}

namespace {

size_t extensionAt(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return std::string::npos;
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos && dot < slash) return std::string::npos;
    return dot;
}

}

std::string convertedName(const std::string& sourcePath, bool toShalimar) {
    if (sourcePath.empty()) return std::string();
    const size_t dot = extensionAt(sourcePath);
    const std::string stem =
        dot == std::string::npos ? sourcePath : sourcePath.substr(0, dot);

    // .shl, the only suffix this editor reads as Shalimar. It wrote .shm until 2026-08-27 - shc
    // takes either - and the file it made then opened as plain text in the editor that had just
    // made it, with no colouring and the wrong compiler behind Build. One suffix that travels.
    return stem + (toShalimar ? ".shl" : ".c");
}

Conversion convert(const std::string& converter, const std::string& sourcePath,
                   const std::string& outputPath, bool toShalimar,
                   LineSink sink, void* context) {
    Conversion result;
    if (converter.empty() || sourcePath.empty() || outputPath.empty()) {
        return result;
    }

    // Written beside the output and moved into place only if it appears, because c2s's exit status
    // cannot say whether it wrote anything: 1 is both "written, with constructs marked BEYOND" and
    // "refused" - what a `#ifndef` gets. Beside it so the move cannot cross a device, and removed first, since a leftover would be read as this run's work.
    const std::string scratch = outputPath + ".new";
    path::remove(scratch);

    const std::string command = "\"" + converter + "\" " +
                                (toShalimar ? "--to-shalimar " : "--to-c ") +
                                "\"" + sourcePath + "\" -o \"" + scratch + "\"";

    result.status = runCaptured(command, result.output, sink, context);
    result.ran = result.status >= 0;

    result.ok = result.ran && result.status == 0;

    if (path::exists(scratch)) {
        // std::rename replaces the destination on Unix and refuses on Windows,
        // so the previous conversion is removed only when the move needs it -
        // and never when this run wrote nothing.
        bool placed = path::rename(scratch, outputPath);
        if (!placed && path::exists(outputPath)) {
            path::remove(outputPath);
            placed = path::rename(scratch, outputPath);
        }
        if (placed) result.produced = outputPath;
        else path::remove(scratch);
    }
    return result;
}

}
