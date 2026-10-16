// Spec: Newton's method on f(y) = 1/y^2 - m (y <- y(3 - m y^2)/2): the error e becomes about 1.5e^2
// at each step, so two steps from the table's 2^-14.4 leave about 2^-55, rounding aside.

#include "SquareRoot.h"
#include "MathBits.h"

namespace rts6x {

double SquareRoot::inverse(double w)
{
    unsigned hi = MathBits::high(w);
    int biased = (int)(hi >> 20);
    // w = m 2^(2h), m in [1, 4): the exponent's last bit (biased even) moves into m.
    int odd = !(biased & 1);
    double m = MathBits::fromWords((hi & 0x000FFFFFu) | (unsigned)(1023 + odd) << 20, MathBits::low(w));
    int i = (odd << 5) | (int)((hi >> 15) & 31);
    double y = base_[i] + slope_[i] * m;
    double hm = 0.5 * m;
    y = y * (1.5 - hm * y * y);
    y = y * (1.5 - hm * y * y);
    return MathBits::scaled(y, -((biased - 1023 - odd) / 2));
}

}  // namespace rts6x
