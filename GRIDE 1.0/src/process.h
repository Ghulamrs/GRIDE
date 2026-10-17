#ifndef EDITOR_PROCESS_H
#define EDITOR_PROCESS_H

#include <atomic>
#include <cstddef>
#include <string>

namespace editor {

class Process {
public:
    Process();
    ~Process();

    bool start(const std::string& command);

    bool startOnConsole(const std::string& command);

    bool running() const { return running_; }

    bool say(const std::string& line);

    std::string readUntil(const std::string& marker, bool* found = 0,
                          int timeoutMs = 30000);

    void stop();

    // Two more ways to start, each taking the child into a group of its own (a job on Windows) so
    // kill() ends it and whatever it started: a command whose output is kept, with no input and
    // its two streams as one; and a program a person talks to, with a real input. README.md, "Input".
    bool startCaptured(const std::string& command);
    bool startInteractive(const std::string& command);

    // Bytes to an interactive child's input, as they are; the end of that input.
    bool send(const char* bytes, size_t size);
    void closeInput();

    // Appends what came next: 1 something (*isStderr: from where), 0 nothing in time, -1 all closed.
    int readAny(std::string& into, bool* isStderr, int timeoutMs);

    // Whether the child has ended, and how, without waiting for it.
    bool ended(int* status);
    // Waits, lets go of all it held, and answers the exit code, or 128 and the signal that ended it.
    int finish();
    // Ends the child and its group at once; safe from another thread while one reads.
    void kill();
    bool killed() const { return killed_.load(); }

private:
    bool startOnPseudoConsole(const std::string& command);

    Process(const Process&);
    Process& operator=(const Process&);

    struct Held;
    Held* held_;

    bool running_;
    std::atomic<bool> killed_;
    std::string pending_;
};

}

#endif
