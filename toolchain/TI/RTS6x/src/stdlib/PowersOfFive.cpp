// Spec: none - written by tools/powers-of-five.py: powers of five for DecimalBinary - 5^b exact for
// b < 28, and C = floor(5^(28a) 2^-k) in [2^63, 2^64) for a = -13 .. 11, limbs least first.

#include "DecimalBinary.h"

namespace rts6x {

const unsigned long long DecimalBinary::fives[28] = {
    0x0000000000000001ull,
    0x0000000000000005ull,
    0x0000000000000019ull,
    0x000000000000007Dull,
    0x0000000000000271ull,
    0x0000000000000C35ull,
    0x0000000000003D09ull,
    0x000000000001312Dull,
    0x000000000005F5E1ull,
    0x00000000001DCD65ull,
    0x00000000009502F9ull,
    0x0000000002E90EDDull,
    0x000000000E8D4A51ull,
    0x0000000048C27395ull,
    0x000000016BCC41E9ull,
    0x000000071AFD498Dull,
    0x0000002386F26FC1ull,
    0x000000B1A2BC2EC5ull,
    0x000003782DACE9D9ull,
    0x00001158E460913Dull,
    0x000056BC75E2D631ull,
    0x0001B1AE4D6E2EF5ull,
    0x000878678326EAC9ull,
    0x002A5A058FC295EDull,
    0x00D3C21BCECCEDA1ull,
    0x0422CA8B0A00A425ull,
    0x14ADF4B7320334B9ull,
    0x6765C793FA10079Dull,
};

const DecimalBinary::Power DecimalBinary::powers[25] = {
    { { 0xFBD14D6Du, 0xE1AFA13Au }, -909, 0 },    // 5^-364
    { { 0x4D8D98B7u, 0xE3E27A44u }, -844, 0 },    // 5^-336
    { { 0x3D1A45DFu, 0xE61ACF03u }, -779, 0 },    // 5^-308
    { { 0x8F5C22C9u, 0xE858AD24u }, -714, 0 },    // 5^-280
    { { 0x23EE8BCBu, 0xEA9C2277u }, -649, 0 },    // 5^-252
    { { 0x4A314EBDu, 0xECE53CECu }, -584, 0 },    // 5^-224
    { { 0x172AACE4u, 0xEF340A98u }, -519, 0 },    // 5^-196
    { { 0xBC3F8CA1u, 0xF18899B1u }, -454, 0 },    // 5^-168
    { { 0xDEC3F126u, 0xF3E2F893u }, -389, 0 },    // 5^-140
    { { 0xF065D37Du, 0xF64335BCu }, -324, 0 },    // 5^-112
    { { 0x88747D94u, 0xF8A95FCFu }, -259, 0 },    // 5^-84
    { { 0xBE068D2Eu, 0xFB158592u }, -194, 0 },    // 5^-56
    { { 0x8300CA0Du, 0xFD87B5F2u }, -129, 0 },    // 5^-28
    { { 0x00000000u, 0x80000000u }, -63, 1 },    // 5^0
    { { 0xF8940984u, 0x813F3978u }, 2, 0 },    // 5^28
    { { 0x81ED449Fu, 0x82818F12u }, 67, 0 },    // 5^56
    { { 0x1AAB65DBu, 0x83C7088Eu }, 132, 0 },    // 5^84
    { { 0x9923329Eu, 0x850FADC0u }, 197, 0 },    // 5^112
    { { 0x5B9BC5C2u, 0x865B8692u }, 262, 0 },    // 5^140
    { { 0x79042286u, 0x87AA9AFFu }, 327, 0 },    // 5^168
    { { 0xF22241E2u, 0x88FCF317u }, 392, 0 },    // 5^196
    { { 0xE33CC92Fu, 0x8A5296FFu }, 457, 0 },    // 5^224
    { { 0xB6409C1Au, 0x8BAB8EEFu }, 522, 0 },    // 5^252
    { { 0x55637EB2u, 0x8D07E334u }, 587, 0 },    // 5^280
    { { 0x5E44FF8Fu, 0x8E679C2Fu }, 652, 0 },    // 5^308
};

}  // namespace rts6x
