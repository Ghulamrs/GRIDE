#ifndef EDITOR_UTF8_H
#define EDITOR_UTF8_H

#include <cstddef>
#include <string>

namespace editor {

namespace utf8 {

bool isContinuation(unsigned char byte);

size_t lengthFrom(unsigned char lead);

size_t startOf(const std::string& text, size_t at);

size_t next(const std::string& text, size_t at);
size_t previous(const std::string& text, size_t at);

size_t count(const std::string& text);

size_t columns(const std::string& text, size_t upTo);

unsigned long codePointAt(const std::string& text, size_t at);

size_t widthOf(unsigned long codePoint);

}
}

#endif
