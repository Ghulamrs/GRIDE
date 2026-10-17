
#pragma once

#include "../debugger.h"

#include "channel.h"

#include <map>
#include <string>
#include <vector>

namespace shalimar {

const char* saysWhereOnly();

const char* saysHowDeepOnly();

const char* releaseHasNoSession();

const char* didNotArm();

class Session {
public:

    // command, where given, is the line that runs it - sim6747 for a C6000 program (sessionCommand in toolchain.h).
    bool start(const std::string& executable, const std::string& command = std::string());
    bool running() const { return channel_.running(); }

    bool ownsTheStop() const { return channel_.running() || exited_; }

    void stop();

    bool breakAt(const std::string& file, size_t line);
    bool clearBreakpoints();

    editor::Stop run();
    editor::Stop resume();
    editor::Stop stepOver();
    editor::Stop stepInto();
    editor::Stop stepOut();

    std::vector<editor::StackFrame> frames();

    std::string printed() { return channel_.printed(); }

private:
    Channel channel_;

    std::map<int, std::string> files_;
    std::vector<std::pair<std::string, size_t> > wanted_;
    std::string program_;
    int unit_ = 0;
    size_t line_ = 0;
    int depth_ = 0;
    bool exited_ = false;
    int status_ = 0;

    editor::Stop listen(int timeoutMs);
    editor::Stop after(const std::string& command);
    int unitFor(const std::string& file) const;
    std::string fileFor(int unit) const;
    void sendWanted();
};

}
