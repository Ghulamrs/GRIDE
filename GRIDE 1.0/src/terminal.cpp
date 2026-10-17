#include "terminal.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdlib>

namespace editor {

Terminal::Terminal() : original_(), raw_(false), eof_(false) {
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &original_) == -1) return;

    struct termios raw = original_;

    raw.c_iflag &= ~(unsigned long)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    raw.c_oflag &= ~(unsigned long)(OPOST);
    raw.c_cflag |= (unsigned long)(CS8);

    raw.c_lflag &= ~(unsigned long)(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) return;
    raw_ = true;
}

Terminal::~Terminal() {
    mouseReporting(false);
    if (raw_) tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
}

// A program run in this terminal may have turned the mouse off on its way out; asking again costs nothing.
void Terminal::reclaim() {
    if (raw_ && mouseOn_) write("\x1b[?1000h\x1b[?1002h\x1b[?1006h");
}

void Terminal::size(int& rows, int& cols) const {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        rows = 24;
        cols = 80;
        return;
    }
    rows = ws.ws_row;
    cols = ws.ws_col;
}

void Terminal::write(const std::string& s) {

    size_t sent = 0;
    while (sent < s.size()) {
        ssize_t n = ::write(STDOUT_FILENO, s.data() + sent, s.size() - sent);
        if (n <= 0) return;
        sent += static_cast<size_t>(n);
    }
}

bool Terminal::readByte(char& c) const {
    ssize_t n = ::read(STDIN_FILENO, &c, 1);

    if (n == 0 && !raw_) eof_ = true;
    return n == 1;
}

}
