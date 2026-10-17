
#include "terminal.h"

#include <windows.h>

namespace editor {

Terminal::Terminal()
    : in_(0), out_(0), inMode_(0), outMode_(0), codePage_(0), raw_(false), eof_(false) {

    codePage_ = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE || hOut == INVALID_HANDLE_VALUE) return;

    DWORD inMode = 0, outMode = 0;
    if (!GetConsoleMode(hIn, &inMode)) return;
    if (!GetConsoleMode(hOut, &outMode)) return;

    in_ = hIn;
    out_ = hOut;
    inMode_ = inMode;
    outMode_ = outMode;

    if (!applyModes()) return;

    raw_ = true;
}

bool Terminal::applyModes() {
    HANDLE hIn = (HANDLE)in_;
    HANDLE hOut = (HANDLE)out_;

    DWORD wantOut = outMode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT;
    if (!SetConsoleMode(hOut, wantOut)) return false;

    DWORD wantIn = inMode_;
    // Quick Edit off, so a click is the editor's rather than the console's selection; the mouse's
    // records on, which readByte turns into the SGR text the other boxes send.
    wantIn &= ~(DWORD)(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT |
                       ENABLE_QUICK_EDIT_MODE);
    wantIn |= ENABLE_EXTENDED_FLAGS | ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_MOUSE_INPUT;
    if (!SetConsoleMode(hIn, wantIn)) {
        SetConsoleMode(hOut, outMode_);
        return false;
    }
    return true;
}

void Terminal::reclaim() {
    if (!raw_) return;
    applyModes();
    SetConsoleOutputCP(CP_UTF8);
}

Terminal::~Terminal() {
    mouseReporting(false);
    if (codePage_ != 0) SetConsoleOutputCP(codePage_);
    if (!raw_) return;
    SetConsoleMode((HANDLE)in_, inMode_);
    SetConsoleMode((HANDLE)out_, outMode_);
}

void Terminal::size(int& rows, int& cols) const {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!out_ || !GetConsoleScreenBufferInfo((HANDLE)out_, &info)) {
        rows = 24;
        cols = 80;
        return;
    }

    cols = info.srWindow.Right - info.srWindow.Left + 1;
    rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    if (cols <= 0) cols = 80;
    if (rows <= 0) rows = 24;
}

void Terminal::write(const std::string& s) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;

    size_t sent = 0;
    while (sent < s.size()) {
        DWORD wrote = 0;
        if (!WriteFile(hOut, s.data() + sent, (DWORD)(s.size() - sent), &wrote, NULL))
            return;
        if (wrote == 0) return;
        sent += wrote;
    }
}

// One console mouse record as the SGR text xterm would send for it, or "" for one the editor has no use
// for: a move with no button held. Positions are the buffer's, made the window's and counted from 1.
static std::string sgrOf(const MOUSE_EVENT_RECORD& m, HANDLE out, unsigned long& held) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    int top = 0, left = 0;
    if (out && GetConsoleScreenBufferInfo(out, &info)) { top = info.srWindow.Top; left = info.srWindow.Left; }
    const int col = m.dwMousePosition.X - left + 1, row = m.dwMousePosition.Y - top + 1;
    int mods = 0;
    if (m.dwControlKeyState & SHIFT_PRESSED) mods |= 4;
    if (m.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) mods |= 16;
    const unsigned long now = m.dwButtonState & 0xFFFF;
    auto text = [&](int code, char final) {
        return "\x1b[<" + std::to_string(code | mods) + ";" + std::to_string(col) + ";" + std::to_string(row) + final;
    };
    if (m.dwEventFlags & MOUSE_WHEELED) {
        short delta = static_cast<short>(HIWORD(m.dwButtonState));
        return text(delta > 0 ? 64 : 65, 'M');
    }
    if (m.dwEventFlags & MOUSE_HWHEELED) return std::string();
    // Left, right and middle, in SGR's numbering: 0, 2, 1.
    const unsigned long bits[3] = { FROM_LEFT_1ST_BUTTON_PRESSED, FROM_LEFT_2ND_BUTTON_PRESSED, RIGHTMOST_BUTTON_PRESSED };
    const int codes[3] = { 0, 1, 2 };
    std::string said;
    if (m.dwEventFlags & MOUSE_MOVED) {
        for (int i = 0; i < 3; ++i) if (now & bits[i]) { said = text(codes[i] | 32, 'M'); break; }
    } else {
        for (int i = 0; i < 3; ++i) {
            bool was = (held & bits[i]) != 0, is = (now & bits[i]) != 0;
            if (is && (!was || (m.dwEventFlags & DOUBLE_CLICK))) said += text(codes[i], 'M');
            else if (!is && was) said += text(codes[i], 'm');
        }
    }
    held = now;
    return said;
}

bool Terminal::readByte(char& c) const {
    if (!pending_.empty()) {
        c = pending_[0];
        pending_.erase(0, 1);
        return true;
    }
    HANDLE hIn = in_ ? (HANDLE)in_ : GetStdHandle(STD_INPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE) return false;

    if (WaitForSingleObject(hIn, 100) != WAIT_OBJECT_0) return false;

    if (raw_) {

        INPUT_RECORD record;
        DWORD seen = 0;
        if (!PeekConsoleInput(hIn, &record, 1, &seen) || seen == 0) return false;
        if (record.EventType == MOUSE_EVENT) {
            ReadConsoleInput(hIn, &record, 1, &seen);
            if (!mouseOn_) return false;
            pending_ = sgrOf(record.Event.MouseEvent, (HANDLE)out_, buttons_);
            if (pending_.empty()) return false;
            c = pending_[0];
            pending_.erase(0, 1);
            return true;
        }
        if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown ||
            record.Event.KeyEvent.uChar.AsciiChar == 0) {
            ReadConsoleInput(hIn, &record, 1, &seen);
            return false;
        }
    }

    DWORD got = 0;
    if (!ReadFile(hIn, &c, 1, &got, NULL)) {
        if (!raw_) eof_ = true;
        return false;
    }
    if (got == 0 && !raw_) eof_ = true;
    return got == 1;
}

}
