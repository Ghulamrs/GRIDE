
#pragma once

#include <string>

namespace shalimar {

class Channel {
public:
    Channel();
    ~Channel();

    bool start(const std::string& command, const std::string& environment);
    bool running() const { return running_; }

    bool say(const std::string& line);

    std::string hear(int timeoutMs, bool* alive = 0);

    std::string printed();

    void stop();

private:
    Channel(const Channel&);
    Channel& operator=(const Channel&);

    struct Held;
    Held* held_;
    bool running_;
    std::string pendingError_;
    std::string pendingOutput_;
};

}
