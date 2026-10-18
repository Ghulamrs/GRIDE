// Spec: ISO C 7.19.3 (streams: buffering, the end-of-file and error indicators, position) and
// 7.19.5.3 (fopen's modes); SPRAB89B 9.18 (the FILE table). A stream is one entry of _ftable.
// Reads are buffered; writes go to the host as they are made, so nothing is ever left to flush.
#ifndef RTS6X_STREAM_H
#define RTS6X_STREAM_H

#include <stdio.h>
#include "rts6x.h"

namespace rts6x {

class Stream {
public:
    // The bits of FILE::flags. A slot of _ftable is free when Open is clear.
    enum Flag { Open = 1, Readable = 2, Writable = 4, AtEnd = 8, Failed = 0x10, OwnBuffer = 0x20,
                Temporary = 0x40, Binary = 0x80 };
    enum { Capacity = 512, FirstFree = 3 };

    explicit Stream(FILE *f) : f_(f) {}

    // A byte as unsigned char, or EOF; out of line, as cpp11 emits every inline member.
    int get();
    int unget(int c);
    size_t read(char *dst, size_t n);

    // The descriptor to write to, read-ahead given back first; -1 (an error) if not opened to write.
    int writeDescriptor();
    // A writer's result: a negative one sets the error indicator. The result, unchanged.
    int wrote(int result) { if (result < 0) f_->flags |= Failed; return result; }

    // Position: fseek and ftell.
    int seek(long offset, int whence);
    long tell();

    // The indicators.
    bool atEnd() const { return (f_->flags & AtEnd) != 0; }
    bool failed() const { return (f_->flags & Failed) != 0; }
    void clear() { f_->flags &= ~(unsigned)(AtEnd | Failed); }

    // Opening: path in mode into slot, or into a free one when slot is null; null on failure.
    static FILE *open(const char *path, const char *mode, FILE *slot);
    // The host file closed, the buffer freed, a temporary file removed, the slot freed: 0 or EOF.
    int close();
    // The name tmpfile gives the file behind slot i, written into name (at least 24 bytes).
    static void temporaryName(int slot, char *name);

private:
    int getSlow();
    // A buffer for the stream, allocated the first time one is needed.
    bool ensureBuffer();
    // Read-ahead dropped, the host's position made the stream's own.
    void giveBack();
    // A text stream's last fill: where it began on the host, and the characters it gave (-1: none).
    void recordFill(long at, int count);
    void forgetFill() { fillCount_[f_ - _ftable] = -1; }
    // The host position of the next character, the host being at end: CR LF read as LF counted.
    long hostPosition(long end);

    static long fillAt_[RTS6X_FTABLE_COUNT];
    static int fillCount_[RTS6X_FTABLE_COUNT];
    // The host flags and the stream's own for mode; false if the mode is not one C allows.
    static bool parseMode(const char *mode, unsigned &host, unsigned &own);

    FILE *f_;
};

}  // namespace rts6x

#endif
