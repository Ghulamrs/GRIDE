// Spec: ISO C 7.20.1.4/2-5 and 7.4.1.10 (white space in the "C" locale): the tables the integer
// conversions read - each character's digit value, and per base the digits that always fit in 32
// bits and the largest magnitude that may still be multiplied.

#include "NumberText.h"

namespace rts6x {

const unsigned char NumberText::digit_[256] = {
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 99, 99, 99, 99, 99, 99,
    99, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 99, 99, 99, 99, 99,
    99, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
};

const unsigned long NumberText::limit_[37] = {
    0, 0, 0xFFFFFFFFu / 2, 0xFFFFFFFFu / 3, 0xFFFFFFFFu / 4, 0xFFFFFFFFu / 5, 0xFFFFFFFFu / 6,
    0xFFFFFFFFu / 7, 0xFFFFFFFFu / 8, 0xFFFFFFFFu / 9, 0xFFFFFFFFu / 10, 0xFFFFFFFFu / 11,
    0xFFFFFFFFu / 12, 0xFFFFFFFFu / 13, 0xFFFFFFFFu / 14, 0xFFFFFFFFu / 15, 0xFFFFFFFFu / 16,
    0xFFFFFFFFu / 17, 0xFFFFFFFFu / 18, 0xFFFFFFFFu / 19, 0xFFFFFFFFu / 20, 0xFFFFFFFFu / 21,
    0xFFFFFFFFu / 22, 0xFFFFFFFFu / 23, 0xFFFFFFFFu / 24, 0xFFFFFFFFu / 25, 0xFFFFFFFFu / 26,
    0xFFFFFFFFu / 27, 0xFFFFFFFFu / 28, 0xFFFFFFFFu / 29, 0xFFFFFFFFu / 30, 0xFFFFFFFFu / 31,
    0xFFFFFFFFu / 32, 0xFFFFFFFFu / 33, 0xFFFFFFFFu / 34, 0xFFFFFFFFu / 35, 0xFFFFFFFFu / 36,
};

// Three spare bytes keep the section a whole number of words: lnk6x misplaced what followed an
// odd-sized .const (2026-10-07).
const unsigned char NumberText::safe_[40] = {
    0, 0, 32, 20, 16, 13, 12, 11, 10, 10, 9, 9, 8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 0, 0, 0,
};

}  // namespace rts6x
