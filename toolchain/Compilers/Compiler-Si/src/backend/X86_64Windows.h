
#pragma once

#include "X86_64.h"

namespace shalimar {

class X86_64WindowsEmitter : public X86_64Emitter {
public:
    X86_64WindowsEmitter() : X86_64Emitter(spelling_impl_, microsoftAbi()) {}

    std::string symbol(const std::string &name) const override { return name; }

    void beginModule(const std::string &sourceName) override;
    void endModule() override;
    void beginFunction(const std::string &name) override;
    void endFunction(int slots) override;
    void label(int id) override;

    // -g in the MASM spelling (-masm=masm): the lines as GRIDE's masm reads them, `.FILE` and
    // `.LINE`, and each described function a PROC FRAME, so a debugger can walk the stack
    void sourceLine(int unit, int line) override;
    void describeFunction(const DebugFunction &function) override;
    void describeGlobals(const std::vector<DebugGlobal> &globals) override { debugGlobals_ = globals; }

private:
    std::string afterPush() const override;
    std::string afterSetFrame() const override;
    std::string afterAlloc(int bytes) const override;
    std::string endPrologue() const override;
    std::string localLabel(const std::string &name) const override;

    std::string labelName(int id) const override { return "Lshm" + std::to_string(id); }
    std::string bytesLabel(int id) const override { return "Lshmb" + std::to_string(id); }
    void openConstSection() override;
    void closeConstSection() override;
    void emitGlobalBlock(int slots) override;
    std::string globalsLabel() const override { return "shm_globals"; }

    MasmSpelling spelling_impl_;
    std::string sourceName_;
    std::string openProcedure_;
    int globalSlots_ = 0;
    int lastUnit_ = -1;        // the last .LINE, so a line is entered once per run of it
    int lastLine_ = -1;
};

}
