// StreamWin.h - the Windows stream layer: what RTS6x's Stream (stdio\Stream.h) is on Windows, and what the
// character functions and CioChannel reach it by (docs\Runtime_Design.md 10.3).
//
// A FILE has a buffer of its own, 4,096 bytes as the Microsoft runtime's, allocated at its first use:
// - Writing (Pending): buf..pos holds bytes already translated (LF as CR LF in text mode) and not yet
//   written. The buffer is written when it fills, by fflush, before a seek or a read, at fclose and at exit.
//   A console and stderr are written as each call ends instead (Unbuffered, or a console descriptor), as
//   the Microsoft runtime's are.
// - Reading: buf..bufend holds raw bytes as the descriptor gave them and pos the next one. A text stream
//   translates as it gives them (TextIn): CR LF as LF, CTRL+Z the end of a file or a pipe. So a position is
//   the descriptor's less the raw bytes still ahead, known without asking Windows again.
// The printf family writes through RTS6x's OutputSink to the descriptor writeDescriptor() answers, which here
// is StreamDescriptor | the stream's slot: CioChannel::write sends those bytes to put().
//
// Locks, as the Microsoft runtime's, once the program has threads (__grt_threads, docs\Runtime_Design.md 9.6):
// - every public function that takes a FILE locks it for the whole call (lock, unlock), so one printf is one
//   piece of the output whichever thread writes; the functions below and Stream's members take no lock, so a call
//   uses as many of them as it likes. feof, ferror and _fileno read one word and take none, as Microsoft's;
// - a free slot of __grt_streams is claimed under streamsLock (freeStream), marked Claimed until its file is open,
//   and the walks of every stream (fflush(0), the end of the program) hold streamsLock, then each stream's lock.
//   Nothing takes streamsLock while it holds a stream's, so the two cannot deadlock;
// - the character functions keep their fast path, the buffer's next byte, when there are no threads; with threads
//   they go to getLocked and putLocked, which take the lock and then the same fast path.
#ifndef GRT_STREAM_WIN_H
#define GRT_STREAM_WIN_H
#include "stdio/Stream.h"
#include "grt.h"
namespace grt {
// The flags beside RTS6x's Stream::Flag (Open 1 ... Binary 0x80).
enum StreamFlag {
    Pending = 0x100,        // the buffer is in writing: buf..pos waits to be written
    Unbuffered = 0x200,     // written as each call ends (stderr)
    TextIn = 0x400,         // the bytes ahead are a text descriptor's, to be translated as they are read
    TextOut = 0x800,        // the bytes being written are a text descriptor's: an LF needs its CR
    Device = 0x1000,        // the descriptor is a device: CTRL+Z is a character, not the end
    Claimed = 0x2000        // the flags alone: the slot is taken by an open still under way
};
enum { BufferSize = 4096, StreamDescriptor = 0x40000000, CtrlZ = 0x1A };
// A null FILE *: the Microsoft runtime's invalid-parameter check, which in a Release build ends the
// program by the fast fail (exit code 0xC0000409) instead of a fault on the null pointer.
inline void valid(const FILE *f) { if (!f) __grt_fail_fast(); }

// The stream locked for one call when the program has threads; whether it was, for unlock.
bool lock(FILE *f);
void unlock(FILE *f, bool held);
// __grt_streams' lock (an SRWLOCK): claiming a slot, and the walks of every stream.
extern void *streamsLock;

bool ensureBuffer(FILE *f);
// The bytes waiting in the buffer written; 0, or EOF with the error indicator set.
int flush(FILE *f);
// count bytes to the stream, translated when its descriptor is text; count, or -1 with the error indicator set.
int put(FILE *f, const char *bytes, unsigned count);
// The next character when the buffer cannot simply give it: a fill, a CR, CTRL+Z, the end; or EOF.
int getSlow(FILE *f);
// The character functions' way when their fast path is not taken (the buffer cannot simply give or take the
// byte, or the program has threads): the stream locked, the fast path tried under the lock, then the slow one.
// c as unsigned char, or EOF.
int getLocked(FILE *f);
int putLocked(int c, FILE *f);
// A stream not in use, its slot numbered and Claimed; null when all 512 are open (errno EMFILE).
FILE *freeStream();
// The stream closed: its bytes written, its descriptor closed, its buffer freed, a temporary file removed, then its
// flags made after - 0 frees the slot, Claimed keeps it for freopen's open. 0, or EOF.
int closeStream(FILE *f, unsigned after);
// At exit: every stream's waiting bytes written, the files fopen opened closed.
void closeAll();
}  // namespace grt
#endif
