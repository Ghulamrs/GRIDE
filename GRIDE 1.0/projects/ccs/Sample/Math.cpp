// hello.cpp

//#define _STD_IO_
#include "Quat.h"

int main(void)
{
    CQuat<> q;
    CVector<> v;
    CMatrix<> m;

#ifdef _STD_IO_
    printf("Hello Math!\n");
    q.print("q");
    v.print("v");
    m.print("m");
#else
    std::cout << "Hello Math!\n";
    std::cout << q << v << m;
#endif

	return 0;
}
