#ifndef EDITOR_TERMINAL_H
#define EDITOR_TERMINAL_H

#ifndef _WIN32
#include <termios.h>
#endif

#include <string>

namespace editor {

enum Key {
    KEY_NONE      = -1,
    KEY_BACKSPACE = 127,
    KEY_ARROW_LEFT = 1000,
    KEY_ARROW_RIGHT,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN,
    KEY_DELETE,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,

    KEY_SHIFT_LEFT,
    KEY_SHIFT_RIGHT,
    KEY_SHIFT_UP,
    KEY_SHIFT_DOWN,
    KEY_SHIFT_HOME,
    KEY_SHIFT_END,
    KEY_SHIFT_PAGE_UP,
    KEY_SHIFT_PAGE_DOWN,

    KEY_CTRL_UP,
    KEY_CTRL_DOWN,

    // A mouse event: Terminal::mouse() says what and where.
    KEY_MOUSE
};

// **The mouse**, as xterm's SGR reporting (?1006) gives it on every box - macOS's and Linux's terminals
// send it, Windows Terminal sends it, and the Windows console's own mouse records are turned into it.
// Rows and columns count from 1, as the escape sequences that draw the screen do.
struct MouseEvent {
    enum Kind { Press, Release, Drag, WheelUp, WheelDown };
    Kind kind = Press;
    int button = 0;          // 0 left, 1 middle, 2 right
    int row = 0, col = 0;
    bool shift = false, ctrl = false;
};

bool isShiftedMove(int key);
int unshifted(int key);

constexpr int ctrl(char c) { return c & 0x1f; }

class Terminal {
public:
    Terminal();
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    void size(int& rows, int& cols) const;

    int readKey() const;

    void reclaim();

    static void write(const std::string& s);

    // The last KEY_MOUSE readKey returned.
    const MouseEvent& mouse() const { return mouse_; }
    // Mouse reporting on or off - off before anything else is given the terminal, and on the way out.
    void mouseReporting(bool on);

    bool eof() const { return eof_; }

private:

    bool readByte(char& c) const;
    int readMouse() const;

    mutable MouseEvent mouse_;
    bool mouseOn_ = false;

#ifdef _WIN32

    bool applyModes();

    void* in_;
    void* out_;
    unsigned long inMode_;
    unsigned long outMode_;
    unsigned int codePage_;
    // Windows' console reports the mouse as records, not text: each is turned into the SGR text the
    // other boxes send and read from here. The buttons held, so a release can be told from a press.
    mutable std::string pending_;
    mutable unsigned long buttons_ = 0;
#else
    struct termios original_;
#endif
    bool raw_;
    mutable bool eof_;
};

}

#endif
