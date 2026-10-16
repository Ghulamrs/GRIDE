// Spec: IEEE 754 binary64, round to nearest; T. J. Dekker, "A floating-point technique for extending
// the available precision" (1971): the exact sum and product of two doubles as an unevaluated sum
// hi + lo, |lo| <= ulp(hi)/2. Every operand here is a normal number; the C674x flushes subnormals.
#ifndef RTS6X_DOUBLE_DOUBLE_H
#define RTS6X_DOUBLE_DOUBLE_H

namespace rts6x {

class DoubleDouble {
public:
    DoubleDouble() : hi(0.0), lo(0.0) {}
    DoubleDouble(double h, double l) : hi(h), lo(l) {}

    // a + b exactly (Knuth's six operations: no ordering asked of a and b).
    static DoubleDouble sum(double a, double b)
    {
        double s = a + b, bv = s - a, av = s - bv;
        return DoubleDouble(s, (a - av) + (b - bv));
    }
    // a + b exactly, where |a| >= |b| or a is zero.
    static DoubleDouble quickSum(double a, double b)
    {
        double s = a + b;
        return DoubleDouble(s, b - (s - a));
    }
    // a * b exactly, each factor split in halves of 26 bits by Veltkamp's constant 2^27 + 1.
    static DoubleDouble product(double a, double b)
    {
        double p = a * b, ah, al, bh, bl;
        split(a, ah, al);
        split(b, bh, bl);
        return DoubleDouble(p, ((ah * bh - p) + ah * bl + al * bh) + al * bl);
    }

    double value() const { return hi + lo; }
    DoubleDouble negated() const { return DoubleDouble(-hi, -lo); }
    DoubleDouble plus(double b) const
    {
        DoubleDouble s = sum(hi, b);
        return quickSum(s.hi, s.lo + lo);
    }
    DoubleDouble plus(const DoubleDouble &b) const
    {
        DoubleDouble s = sum(hi, b.hi), t = sum(lo, b.lo);
        s = quickSum(s.hi, s.lo + t.hi);
        return quickSum(s.hi, s.lo + t.lo);
    }
    DoubleDouble minus(const DoubleDouble &b) const { return plus(b.negated()); }
    DoubleDouble times(double b) const
    {
        DoubleDouble p = product(hi, b);
        return quickSum(p.hi, p.lo + lo * b);
    }
    DoubleDouble times(const DoubleDouble &b) const
    {
        DoubleDouble p = product(hi, b.hi);
        return quickSum(p.hi, p.lo + (hi * b.lo + lo * b.hi));
    }
    // this / d: three quotient digits, each corrected by the exact remainder; about 2^-100 relative.
    DoubleDouble over(const DoubleDouble &d) const;

    double hi, lo;

private:
    static void split(double a, double &h, double &l)
    {
        double c = 134217729.0 * a;
        h = c - (c - a);
        l = a - h;
    }
};

}  // namespace rts6x

#endif
