// hello.cpp - extended: matrix and vector products

//#define _STD_IO_
#include "Quat.h"

int main(void)
{
    CQuat<> q;
    CVector<> v;
    CMatrix<> m;

    std::cout << "Hello Math!\n";
    std::cout << q << v << m;

    // Two 3x3 matrices multiplied into a third
    CMatrix<> A(CVector<>(1, 2, 3), CVector<>(4, 5, 6), CVector<>(7, 8, 10));
    CMatrix<> B(CVector<>(2, 0, 1), CVector<>(1, 3, 2), CVector<>(0, 1, 4));
    CMatrix<> C = A * B;
    std::cout << "A =\n" << A << "B =\n" << B << "C = A * B =\n" << C;

    // Two 3-vectors: cross, dot and component products
    CVector<> a(1, 2, 3);
    CVector<> b(4, 5, 6);
    CVector<> axb = a * b;
    std::cout << "a =" << a << "b =" << b;
    std::cout << "a * b =" << axb;
    std::cout << "a . b = " << (a % b) << "\n";
    std::cout << "a ^ b =" << (a ^ b);

    // A matrix times a vector, and a rotation about Z by 0.5 rad
    CVector<> Aa = A * a;
    std::cout << "A * a =" << Aa;
    CMatrix<> Rz = CRotrix<>::aboutZ(0.5);
    std::cout << "Rz(0.5) =\n" << Rz << "Rz * a =" << (Rz * a);

    return 0;
}
