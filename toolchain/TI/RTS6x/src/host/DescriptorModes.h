// Spec: Microsoft C runtime, _setmode - a descriptor's translation mode, _O_TEXT or _O_BINARY, the
// previous one answered; and the C$$IO$$ host's open, which names the descriptor the target chose.
// The mode is only remembered here (the host translates by its open flags); 0 marks a closed one.
#ifndef RTS6X_DESCRIPTOR_MODES_H
#define RTS6X_DESCRIPTOR_MODES_H

namespace rts6x {

class DescriptorModes {
public:
    enum { Text = 0x4000, Binary = 0x8000, Count = 32 };
    // The mode now in force for fd, setting it to mode: the old one, or -1 with errno set.
    static int exchange(int fd, int mode);
    // The lowest descriptor from 3 that is not open, for the host's open to name; -1 if none is free.
    static int vacant();
    // The host channel's open and close say so here; 0, 1 and 2 are open from the start.
    static void opened(int fd, int mode) { if (fd >= 0 && fd < Count) { modes_[fd] = mode; alias_[fd] = 0; } }
    static void closed(int fd) { if (fd >= 0 && fd < Count) { modes_[fd] = 0; alias_[fd] = 0; } }
    // fd's mode, 0 when it is not open.
    static int modeOf(int fd) { return fd >= 0 && fd < Count ? modes_[fd] : 0; }
    // **The host's descriptor behind fd**: fd itself, or the one _dup or _dup2 made it share (Microsoft's
    // descriptor calls, docs\Runtime_Design.md 9.4). The channel names this one to the host.
    static int host(int fd) { return fd >= 0 && fd < Count && alias_[fd] != 0 ? alias_[fd] - 1 : fd; }
    // Whether another open descriptor shares fd's host descriptor: closing fd then leaves the host's open.
    static bool shared(int fd);
    // to opened as a second name for from's host descriptor, in from's mode.
    static void share(int to, int from) { modes_[to] = modes_[from]; alias_[to] = static_cast<unsigned char>(host(from) + 1); }
private:
    static int modes_[Count];
    static unsigned char alias_[Count];
};

}  // namespace rts6x

#endif
