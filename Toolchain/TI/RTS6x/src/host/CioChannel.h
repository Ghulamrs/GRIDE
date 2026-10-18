// Spec: the C$$IO$$ host channel (sim6747 src/C6xHost.cpp): a request in _CIOBUF_ is
// [length:4][command:1][parameters:8][data], the answer [length:4][parameters:8][data], little-endian.
// One class owns the buffer; the C library's file calls go through it and nothing else touches it.
#ifndef RTS6X_CIO_CHANNEL_H
#define RTS6X_CIO_CHANNEL_H

extern "C" {
extern unsigned char _CIOBUF_[];
void __rts6x_cio_trap(void);
}

namespace rts6x {

class CioChannel {
public:
    // The commands the host answers, in its numbering.
    enum Command { Open = 0xF0, Close = 0xF1, Read = 0xF2, Write = 0xF3, Lseek = 0xF4,
                   Unlink = 0xF5, Getenv = 0xF6, Rename = 0xF7, GetTime = 0xF8, GetClock = 0xF9 };
    // The most data one request carries; _CIOBUF_ is this plus the two headers, rounded up.
    enum { DataCapacity = 1024 };

    // The host's open flags (its file.h numbering).
    enum OpenFlag { ReadOnly = 0, WriteOnly = 1, ReadWrite = 2, Append = 8, Create = 0x200,
                    Truncate = 0x400, Binary = 0x8000 };
    // The three origins lseek takes, numbered as SEEK_SET, SEEK_CUR and SEEK_END.
    enum Origin { FromStart = 0, FromHere = 1, FromEnd = 2 };

    // count bytes to host descriptor fd, in as many requests as it takes: the number, or -1.
    static int write(int fd, const char *bytes, unsigned count);
    // At most count (and DataCapacity) bytes in one request: the number, 0 at the end, or -1.
    static int read(int fd, char *bytes, unsigned count);
    // A descriptor for path, chosen here and named to the host in the request, or -1.
    static int open(const char *path, unsigned flags);
    static int close(int fd);
    // The new position from the start of the file, or -1.
    static long seek(int fd, long offset, Origin origin);
    static int unlink(const char *path);
    static int rename(const char *from, const char *to);
    // The host's value of name into value (capacity bytes with the NUL); false if unset.
    static bool environment(const char *name, char *value, unsigned capacity);
    // Seconds since 1970 by the host's clock, and processor cycles since the program started.
    static unsigned long hostTime();
    static unsigned long cycles();

private:
    enum { RequestHeader = 13, ReplyHeader = 12 };
    static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
    static void put32(unsigned char *p, unsigned v) { put16(p, v); put16(p + 2, v >> 16); }
    static unsigned get16(const unsigned char *p) { return p[0] | (p[1] << 8); }
    static unsigned long get32(const unsigned char *p) { return get16(p) | ((unsigned long)get16(p + 2) << 16); }
    // A signed 16-bit answer, which is how the host says -1.
    static int answer16() { return (int)(short)get16(_CIOBUF_ + 4); }
    // A request whose data is a NUL-terminated string (two, for rename), its NULs sent too.
    static void requestText(Command c, const unsigned char params[8], const char *a, const char *b);
    // One request: the header and the data copied into _CIOBUF_, then the trap; the answer is
    // left in the buffer for the caller to read.
    static void request(Command c, const unsigned char params[8], const char *data, unsigned length);
};

}  // namespace rts6x

#endif
