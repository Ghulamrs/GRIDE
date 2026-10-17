#include "session.h"

#include <cstdlib>
#include <cstring>

namespace shalimar {

const char* saysWhereOnly() {
    return "a Shalimar program says where it is, not what is in it";
}

const char* saysHowDeepOnly() {
    return "a Shalimar program reports how deep it is, not what called it";
}

const char* releaseHasNoSession() {
    return "release links a runtime with no debugger in it";
}

const char* didNotArm() {
    return "the program did not arm - it has no debugger in it";
}

namespace {

std::string leafOf(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool startsWith(const std::string& text, const char* head) {
    size_t n = std::strlen(head);
    return text.size() >= n && text.compare(0, n, head) == 0;
}

bool three(const std::string& line, size_t from, int& a, int& b, int& c) {
    int* into[3] = {&a, &b, &c};
    size_t at = from;
    for (int which = 0; which < 3; ++which) {
        while (at < line.size() && line[at] == ' ') ++at;
        if (at >= line.size() || line[at] < '0' || line[at] > '9') return false;
        int value = 0;
        while (at < line.size() && line[at] >= '0' && line[at] <= '9') {
            value = value * 10 + (line[at] - '0');
            ++at;
        }
        *into[which] = value;
    }
    return true;
}

}

bool Session::start(const std::string& executable, const std::string& command) {
    stop();
    program_ = executable;
    exited_ = false;
    status_ = 0;
    files_.clear();

    std::string quoted = executable;
#ifdef _WIN32
    quoted = "\"" + executable + "\"";
#else
    quoted = "'" + executable + "'";
#endif
    if (!command.empty()) quoted = command;
    if (!channel_.start(quoted, "SHM_DEBUG=1")) return false;

    for (;;) {
        bool alive = true;
        std::string line = channel_.hear(5000, &alive);
        if (line.empty() && !alive) return false;
        if (line == "#ready") return true;
        if (startsWith(line, "#file ")) {
            size_t at = 6;
            int unit = 0;
            while (at < line.size() && line[at] >= '0' && line[at] <= '9') {
                unit = unit * 10 + (line[at] - '0');
                ++at;
            }
            while (at < line.size() && line[at] == ' ') ++at;
            files_[unit] = line.substr(at);
            continue;
        }
        if (line.empty()) return false;
    }
}

void Session::stop() {
    if (channel_.running()) {
        channel_.say("q");
        channel_.stop();
    }
    wanted_.clear();
    files_.clear();
    exited_ = false;
}

int Session::unitFor(const std::string& file) const {
    const std::string leaf = leafOf(file);
    for (std::map<int, std::string>::const_iterator it = files_.begin();
         it != files_.end(); ++it) {
        if (it->second == leaf) return it->first;
    }
    return -1;
}

std::string Session::fileFor(int unit) const {
    std::map<int, std::string>::const_iterator at = files_.find(unit);
    return at == files_.end() ? std::string() : at->second;
}

bool Session::breakAt(const std::string& file, size_t line) {
    wanted_.push_back(std::make_pair(file, line));
    if (!channel_.running()) return true;
    const int unit = unitFor(file);
    if (unit < 0) return false;
    char command[64];
    std::snprintf(command, sizeof command, "b %d %d", unit, static_cast<int>(line));
    return channel_.say(command);
}

bool Session::clearBreakpoints() {
    for (size_t i = 0; i < wanted_.size(); ++i) {
        const int unit = unitFor(wanted_[i].first);
        if (unit < 0 || !channel_.running()) continue;
        char command[64];
        std::snprintf(command, sizeof command, "d %d %d", unit,
                      static_cast<int>(wanted_[i].second));
        channel_.say(command);
    }
    wanted_.clear();
    return true;
}

void Session::sendWanted() {
    for (size_t i = 0; i < wanted_.size(); ++i) {
        const int unit = unitFor(wanted_[i].first);
        if (unit < 0) continue;
        char command[64];
        std::snprintf(command, sizeof command, "b %d %d", unit,
                      static_cast<int>(wanted_[i].second));
        channel_.say(command);
    }
}

editor::Stop Session::listen(int timeoutMs) {
    editor::Stop where;
    for (;;) {
        bool alive = true;
        std::string line = channel_.hear(timeoutMs, &alive);

        if (startsWith(line, "#stop ")) {
            int unit = 0, at = 0, depth = 0;
            if (!three(line, 6, unit, at, depth)) continue;
            unit_ = unit;
            line_ = static_cast<size_t>(at);
            depth_ = depth;
            where.stopped = true;
            where.file = fileFor(unit);
            where.line = line_;
            where.said = channel_.printed();
            return where;
        }
        if (startsWith(line, "#exit ")) {
            int status = 0, ignored = 0, alsoIgnored = 0;
            three(line + " 0 0", 6, status, ignored, alsoIgnored);
            exited_ = true;
            status_ = status;
            where.exited = true;
            where.status = status;
            where.said = channel_.printed();
            channel_.stop();
            return where;
        }
        if (line.empty() && !alive) {

            exited_ = true;
            where.exited = true;
            where.status = status_;
            where.said = channel_.printed();
            return where;
        }
        if (line.empty()) {
            where.said = channel_.printed();
            return where;
        }
    }
}

editor::Stop Session::after(const std::string& command) {
    if (!channel_.running()) {
        editor::Stop gone;
        gone.exited = exited_;
        gone.status = status_;
        return gone;
    }
    channel_.say(command);
    return listen(30000);
}

editor::Stop Session::run() {
    sendWanted();
    return after("c");
}

editor::Stop Session::resume() { return after("c"); }
editor::Stop Session::stepOver() { return after("n"); }
editor::Stop Session::stepInto() { return after("s"); }
editor::Stop Session::stepOut() { return after("o"); }

std::vector<editor::StackFrame> Session::frames() {
    std::vector<editor::StackFrame> stack;
    if (!channel_.running()) return stack;

    editor::StackFrame here;
    here.file = fileFor(unit_);
    here.line = line_;

    char said[48];
    std::snprintf(said, sizeof said, "%d call%s deep", depth_, depth_ == 1 ? "" : "s");
    here.function = said;
    stack.push_back(here);
    return stack;
}

}
