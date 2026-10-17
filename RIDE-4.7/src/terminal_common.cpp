
#include "terminal.h"

#include <cstdlib>

namespace editor {

namespace {

bool hasShift(int modifier) {
    if (modifier < 2) return false;
    return ((modifier - 1) & 1) != 0;
}

bool hasControl(int modifier) {
    if (modifier < 2) return false;
    return ((modifier - 1) & 4) != 0;
}

int shiftedForm(int key) {
    switch (key) {
        case KEY_ARROW_LEFT:  return KEY_SHIFT_LEFT;
        case KEY_ARROW_RIGHT: return KEY_SHIFT_RIGHT;
        case KEY_ARROW_UP:    return KEY_SHIFT_UP;
        case KEY_ARROW_DOWN:  return KEY_SHIFT_DOWN;
        case KEY_HOME:        return KEY_SHIFT_HOME;
        case KEY_END:         return KEY_SHIFT_END;
        case KEY_PAGE_UP:     return KEY_SHIFT_PAGE_UP;
        case KEY_PAGE_DOWN:   return KEY_SHIFT_PAGE_DOWN;
        default:              return key;
    }
}

int controlledForm(int key) {
    switch (key) {
        case KEY_ARROW_UP:   return KEY_CTRL_UP;
        case KEY_ARROW_DOWN: return KEY_CTRL_DOWN;
        default:             return key;
    }
}

void parameters(const std::string& text, int& first, int& second) {
    first = 1;
    second = 1;
    if (text.empty()) return;

    size_t semi = text.find(';');
    first = std::atoi(text.c_str());
    if (first == 0) first = 1;
    if (semi != std::string::npos) {
        second = std::atoi(text.c_str() + semi + 1);
        if (second == 0) second = 1;
    }
}

int fromLetter(char letter) {
    switch (letter) {
        case 'A': return KEY_ARROW_UP;
        case 'B': return KEY_ARROW_DOWN;
        case 'C': return KEY_ARROW_RIGHT;
        case 'D': return KEY_ARROW_LEFT;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        case 'P': return KEY_F1;
        case 'Q': return KEY_F2;
        case 'R': return KEY_F3;
        case 'S': return KEY_F4;
        default:  return '\x1b';
    }
}

int fromNumber(int number) {
    switch (number) {
        case 1:  return KEY_HOME;
        case 3:  return KEY_DELETE;
        case 4:  return KEY_END;
        case 5:  return KEY_PAGE_UP;
        case 6:  return KEY_PAGE_DOWN;
        case 7:  return KEY_HOME;
        case 8:  return KEY_END;
        case 11: return KEY_F1;
        case 12: return KEY_F2;
        case 13: return KEY_F3;
        case 14: return KEY_F4;
        case 15: return KEY_F5;
        case 17: return KEY_F6;
        case 18: return KEY_F7;
        case 19: return KEY_F8;
        case 20: return KEY_F9;
        case 21: return KEY_F10;
        default: return '\x1b';
    }
}

}

bool isShiftedMove(int key) {
    return key >= KEY_SHIFT_LEFT && key <= KEY_SHIFT_PAGE_DOWN;
}

int unshifted(int key) {
    switch (key) {
        case KEY_SHIFT_LEFT:      return KEY_ARROW_LEFT;
        case KEY_SHIFT_RIGHT:     return KEY_ARROW_RIGHT;
        case KEY_SHIFT_UP:        return KEY_ARROW_UP;
        case KEY_SHIFT_DOWN:      return KEY_ARROW_DOWN;
        case KEY_SHIFT_HOME:      return KEY_HOME;
        case KEY_SHIFT_END:       return KEY_END;
        case KEY_SHIFT_PAGE_UP:   return KEY_PAGE_UP;
        case KEY_SHIFT_PAGE_DOWN: return KEY_PAGE_DOWN;
        default:                  return key;
    }
}

// ESC [ < b ; x ; y M (press or drag) or m (release), the [ < already read.
int Terminal::readMouse() const {
    std::string params;
    char final = 0;
    for (int i = 0; i < 24; ++i) {
        char b = 0;
        if (!readByte(b)) return '\x1b';
        if ((b >= '0' && b <= '9') || b == ';') { params += b; continue; }
        final = b;
        break;
    }
    if (final != 'M' && final != 'm') return '\x1b';
    int field[3] = {0, 0, 0};
    size_t at = 0;
    for (int i = 0; i < 3; ++i) {
        field[i] = std::atoi(params.c_str() + at);
        size_t semi = params.find(';', at);
        if (semi == std::string::npos) { if (i < 2) return '\x1b'; break; }
        at = semi + 1;
    }
    const int code = field[0];
    MouseEvent m;
    m.col = field[1];
    m.row = field[2];
    m.shift = (code & 4) != 0;
    m.ctrl = (code & 16) != 0;
    m.button = code & 3;
    if (code & 64) {
        m.kind = (code & 1) ? MouseEvent::WheelDown : MouseEvent::WheelUp;
    } else if (final == 'm') {
        m.kind = MouseEvent::Release;
    } else {
        m.kind = (code & 32) ? MouseEvent::Drag : MouseEvent::Press;
    }
    mouse_ = m;
    return KEY_MOUSE;
}

void Terminal::mouseReporting(bool on) {
    if (!raw_ || on == mouseOn_) return;
    // 1000 presses and releases, 1002 drags with a button held, 1006 the SGR form, whose coordinates have no limit.
    write(on ? "\x1b[?1000h\x1b[?1002h\x1b[?1006h" : "\x1b[?1006l\x1b[?1002l\x1b[?1000l");
    mouseOn_ = on;
}

int Terminal::readKey() const {
    char c = 0;
    if (!readByte(c)) return KEY_NONE;
    if (c != '\x1b') return static_cast<unsigned char>(c);

    char next = 0;
    if (!readByte(next)) return '\x1b';

    if (next == 'O') {
        char letter = 0;
        if (!readByte(letter)) return '\x1b';
        return fromLetter(letter);
    }

    if (next != '[') return '\x1b';

    std::string params;
    char first0 = 0;
    if (!readByte(first0)) return '\x1b';
    if (first0 == '<') return readMouse();
    char final = 0;
    for (int i = 0; i < 16; ++i) {
        char b = 0;
        if (i == 0) b = first0;
        else if (!readByte(b)) return '\x1b';
        if ((b >= '0' && b <= '9') || b == ';') {
            params += b;
            continue;
        }
        final = b;
        break;
    }
    if (final == 0) return '\x1b';

    int first = 1, second = 1;
    parameters(params, first, second);

    int key;
    if (final == '~') {
        key = fromNumber(first);
    } else {
        key = fromLetter(final);
    }
    if (key == '\x1b') return '\x1b';

    if (hasControl(second)) return controlledForm(key);
    return hasShift(second) ? shiftedForm(key) : key;
}

}
