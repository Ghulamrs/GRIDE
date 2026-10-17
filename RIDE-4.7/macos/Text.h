// The two spellings of text the window deals in: NSString on the AppKit side, UTF-8 char* on the
// core's (winforms/bridge.h). Every crossing goes through one of these, so an embedded nil or a
// NULL from the core is handled in one place rather than at every call.
#ifndef MACOS_TEXT_H
#define MACOS_TEXT_H

#import <Foundation/Foundation.h>

#include <string>

#include "bridge.h"

// What the core said, as an NSString; never nil.
static inline NSString* Str(const char* text) {
    if (text == NULL) return @"";
    NSString* made = [NSString stringWithUTF8String:text];
    return made != nil ? made : @"";
}

// What a compiler or a program printed, which may not be UTF-8: read as UTF-8 where it is, and
// byte for byte as Latin-1 where it is not, which never fails - so the Output tab is never empty
// for one bad byte (H4). Paths go through Str, which does not guess.
static inline NSString* StrLossy(const char* bytes, size_t size) {
    if (bytes == NULL || size == 0) return @"";
    NSString* made = [[NSString alloc] initWithBytes:bytes length:size encoding:NSUTF8StringEncoding];
    if (made == nil)
        made = [[NSString alloc] initWithBytes:bytes length:size encoding:NSISOLatin1StringEncoding];
    return made != nil ? made : @"";
}

static inline NSString* StrLossy(const std::string& text) { return StrLossy(text.data(), text.size()); }

// The bytes of `pending` up to the last whole UTF-8 character, taken out of it: a program's output
// arrives in pieces, and a character cut between two of them is kept for the next.
static inline std::string WholeCharacters(std::string& pending) {
    size_t end = pending.size();
    size_t back = 0;
    while (back < 3 && back < end && (static_cast<unsigned char>(pending[end - 1 - back]) & 0xC0) == 0x80)
        ++back;
    if (back < end) {
        unsigned char lead = static_cast<unsigned char>(pending[end - 1 - back]);
        size_t wants = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (wants > back + 1) end -= back + 1;
    }
    std::string whole = pending.substr(0, end);
    pending.erase(0, end);
    return whole;
}

// The same for a string the core allocated and hands over to be freed.
static inline NSString* Take(char* text) {
    NSString* made = Str(text);
    if (text != NULL) ride_free(text);
    return made;
}

// An NSString as the core wants it. The pointer lives as long as the
// autorelease pool, which is the call it is handed to.
static inline const char* Utf8(NSString* text) {
    if (text == nil) return "";
    const char* bytes = text.UTF8String;
    return bytes != NULL ? bytes : "";
}

// A copy that can cross to another thread.
static inline std::string StdString(NSString* text) { return std::string(Utf8(text)); }

#endif
