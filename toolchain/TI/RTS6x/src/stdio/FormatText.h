// Spec: ISO C 7.19.6.1/3 and 7.24.2.1/3 - a format is a sequence of characters, narrow for printf,
// wide for wprintf. One reader for both, so a single formatter serves the whole family.
#ifndef RTS6X_FORMAT_TEXT_H
#define RTS6X_FORMAT_TEXT_H

namespace rts6x {

class FormatText {
public:
    explicit FormatText(const char *s) : narrow_(s), wide_(0) {}
    explicit FormatText(const wchar_t *s) : narrow_(0), wide_(s) {}

    // The character at the reading point, as an unsigned value; 0 at the end.
    unsigned at() const { return narrow_ ? (unsigned char)*narrow_ : (unsigned)*wide_; }
    void next() { if (narrow_) narrow_++; else wide_++; }
    bool done() const { return at() == 0; }
    bool wide() const { return wide_ != 0; }
    // The narrow text from the reading point, or null for a wide one; the character k ahead.
    const char *narrowText() const { return narrow_; }
    unsigned at(unsigned k) const { return narrow_ ? (unsigned char)narrow_[k] : (unsigned)wide_[k]; }
    void skip(unsigned n) { if (narrow_) narrow_ += n; else wide_ += n; }

private:
    const char *narrow_;
    const wchar_t *wide_;
};

}  // namespace rts6x

#endif
