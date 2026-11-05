// Spec: ISO C99 7.12.8.3 lgamma - the Taylor series about 1 and 2 (GammaFunction.h), the first two coefficients as
// double-doubles and the rest in double.

#include "GammaFunction.h"
#include "ExtendedConstants.h"

namespace {

// zeta(k)/k and (zeta(k) - 1)/k, k = 2 .. 29: k = 2 as a double-double, the rest their high parts
const unsigned long long zetaOverK[28] = {
    0x3FEA51A6625307D3ULL, 0x3FD9A4D55BEAB2D7ULL, 0x3FD151322AC7D848ULL, 0x3FCA8B9C17AA6149ULL, 0x3FC5B40CB100C306ULL,
    0x3FC2703A1DCEA3AEULL, 0x3FC010B36AF86397ULL, 0x3FBC806706D57DB4ULL, 0x3FB9A01E385D5F8FULL, 0x3FB748C33114C6D6ULL,
    0x3FB556AD63243BC4ULL, 0x3FB3B1D971FC5985ULL, 0x3FB2496DF8320C5FULL, 0x3FB11133476E7FE0ULL, 0x3FB00010064CDEB2ULL,
    0x3FAE1E2D311E8ABDULL, 0x3FAC71CE3A20B419ULL, 0x3FAAF28A1B5688A0ULL, 0x3FA9999B3352D5BAULL, 0x3FA86186DB77BFBFULL,
    0x3FA745D1D1778DF9ULL, 0x3FA642C88591B66DULL, 0x3FA555556AAAFDCDULL, 0x3FA47AE151EB9FB7ULL, 0x3FA3B13B189D925EULL,
    0x3FA2F684C00002BCULL, 0x3FA24924936DB7BCULL, 0x3FA1A7B961A7B9AAULL };
const unsigned long long zetaLessOneOverK[28] = {
    0x3FD4A34CC4A60FA6ULL, 0x3FB13E001A557607ULL, 0x3F951322AC7D8483ULL, 0x3F7E404FC218F5F2ULL, 0x3F67ADD6EADB6C30ULL,
    0x3F538AC5C2BF8E08ULL, 0x3F40B36AF86396E9ULL, 0x3F2D3FD4C76D2FC8ULL, 0x3F1A127B0F17D65AULL, 0x3F078DE5BD7C81EFULL,
    0x3EF580DCEE66EB02ULL, 0x3EE3CBC963CE2243ULL, 0x3ED2597A39F34AACULL, 0x3EC11B2EB7679541ULL, 0x3EB0064CDEB22F0FULL,
    0x3E9E2600D93CFD2FULL, 0x3E8C76BBB3F07A4DULL, 0x3E7AF5A6CBBF8A97ULL, 0x3E699B93C2070B0FULL, 0x3E5862C734DF3EACULL,
    0x3E47469DACCFADCDULL, 0x3E36434A8447AEADULL, 0x3E2555A877FFD2C3ULL, 0x3E147B1679258D0EULL, 0x3E03B15D2B2FC10CULL,
    0x3DF2F69A9FABE3E0ULL, 0x3DE24932A337434CULL, 0x3DD1A7C26EC2523CULL };
const unsigned long long zetaTwoLow = 0x3C71873D8912200CULL;   // the low part of zeta(2)/2 and (zeta(2) - 1)/2

}  // namespace

namespace rts6x {

double GammaFunction::nearRoot(double e, const DoubleDouble &c1, const unsigned long long *table)
{
    double tail = 0.0;
    for (int k = 29; k >= 3; k--) {
        double c = MathBits::from(table[k - 2]);
        tail = tail * e + ((k & 1) ? -c : c);
    }
    DoubleDouble second = ExtendedConstants::of(table[0], zetaTwoLow);
    DoubleDouble r = c1.times(e).plus(second.times(e).times(e));
    return r.plus(tail * e * e * e).value();
}

double GammaFunction::nearOne(double e)
{
    return nearRoot(e, ExtendedConstants::eulerGamma().negated(), zetaOverK);
}

double GammaFunction::nearTwo(double e)
{
    return nearRoot(e, ExtendedConstants::oneLessGamma(), zetaLessOneOverK);
}

}  // namespace rts6x
