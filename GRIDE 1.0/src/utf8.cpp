#include "utf8.h"

namespace editor {
namespace utf8 {

bool isContinuation(unsigned char byte) { return (byte & 0xC0) == 0x80; }

size_t lengthFrom(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;

    return 1;
}

size_t startOf(const std::string& text, size_t at) {
    if (at > text.size()) at = text.size();
    while (at > 0 && isContinuation(static_cast<unsigned char>(text[at - 1])) &&
           isContinuation(static_cast<unsigned char>(text[at])))
        --at;

    while (at > 0 && at < text.size() &&
           isContinuation(static_cast<unsigned char>(text[at])))
        --at;
    return at;
}

size_t next(const std::string& text, size_t at) {
    if (at >= text.size()) return text.size();
    size_t step = lengthFrom(static_cast<unsigned char>(text[at]));
    size_t to = at + step;

    while (to < text.size() && isContinuation(static_cast<unsigned char>(text[to]))) ++to;
    return to > text.size() ? text.size() : to;
}

size_t previous(const std::string& text, size_t at) {
    if (at == 0) return 0;
    if (at > text.size()) at = text.size();

    size_t back = at - 1;
    while (back > 0 && isContinuation(static_cast<unsigned char>(text[back]))) --back;
    return back;
}

size_t count(const std::string& text) {
    size_t n = 0;
    for (size_t at = 0; at < text.size(); at = next(text, at)) ++n;
    return n;
}

unsigned long codePointAt(const std::string& text, size_t at) {
    if (at >= text.size()) return 0;

    unsigned char lead = static_cast<unsigned char>(text[at]);
    size_t length = lengthFrom(lead);
    if (length == 1) return lead;
    if (at + length > text.size()) return lead;

    unsigned long value = lead & (0xFF >> (length + 1));
    for (size_t i = 1; i < length; ++i) {
        unsigned char part = static_cast<unsigned char>(text[at + i]);
        if (!isContinuation(part)) return lead;
        value = (value << 6) | (part & 0x3F);
    }
    return value;
}

size_t widthOf(unsigned long code) {

    if ((code >= 0x0300 && code <= 0x036F) || (code >= 0x064B && code <= 0x065F) ||
        (code >= 0x0670 && code <= 0x0670) || (code >= 0x06D6 && code <= 0x06ED) ||
        (code >= 0x200B && code <= 0x200F))
        return 0;

    if ((code >= 0x1100 && code <= 0x115F) ||
        (code >= 0x2E80 && code <= 0x303E) ||
        (code >= 0x3041 && code <= 0x33FF) ||
        (code >= 0x3400 && code <= 0x4DBF) ||
        (code >= 0x4E00 && code <= 0x9FFF) ||
        (code >= 0xA000 && code <= 0xA4CF) ||
        (code >= 0xAC00 && code <= 0xD7A3) ||
        (code >= 0xF900 && code <= 0xFAFF) ||
        (code >= 0xFE30 && code <= 0xFE6F) ||
        (code >= 0xFF00 && code <= 0xFF60) ||
        (code >= 0xFFE0 && code <= 0xFFE6) ||
        (code >= 0x1F300 && code <= 0x1F64F) ||
        (code >= 0x1F900 && code <= 0x1F9FF) ||
        (code >= 0x20000 && code <= 0x3FFFD))
        return 2;

    return 1;
}

size_t columns(const std::string& text, size_t upTo) {
    if (upTo > text.size()) upTo = text.size();

    size_t width = 0;
    for (size_t at = 0; at < upTo;) {
        width += widthOf(codePointAt(text, at));
        size_t step = next(text, at);
        at = (step > at) ? step : at + 1;
    }
    return width;
}

}
}
