//
// CQuat.h: A CQuat class declaration & definition.
//			1 param template-based CQuaternion class
//////////////////////////////////////////////////////

#ifndef Quat_h
#define Quat_h

#include "Matrix.h"

template<size_t I=4>
class CQuat: public CVector<I> {
public:
	CQuat<I>() : CVector<I>() {
		assert(I==4);
		this->vector[0] = 1.0;
	}

    CQuat<I>(double q0, double q1, double q2, double q3) :
		CVector<I>(q0, q1, q2, q3) {
    }

	CQuat<I>(CVector<I>& v) : CVector<I>(v) {
	}

	// 2-Quaternion multiplication
	//CQuat<I> operator*(const CQuat<I> &rhs) const {
	//	double    r, s, t;
	//	CVector<> p, q, v;
	//	CQuat<I>  lhs(*this);

	//	s = lhs[0];
	//	t = rhs[0];
	//	p = CVector<>(lhs[1], lhs[2], lhs[3]);
	//	q = CVector<>(rhs[1], rhs[2], rhs[3]);
	//	r = s * t - p % q;
	//	v = s * q + t * p + p * q;
	//	return CQuat<I>(r, v[0], v[1], v[2]);
	//}
#ifdef _STD_IO_
	void print(char *title = "") {
		printf("\n%s\t", title);
		this->printrow();
	}
#else
    friend std::ostream& operator<<(std::ostream& os, const CQuat<I> &rhs) {
        for(size_t i=0; i<I; i++) os << " " << rhs.vector[i];
        return os << std::endl;
    }
#endif
};

// Quat Rotations
template <size_t I = 4>
class QRotation : public CQuat<I> { // Quaternion rotations
public:
	static CQuat<I> aboutX(double angle) {
		return CQuat<I>(cos(angle / 2), sin(angle / 2), 0.0, 0.0);
	}

	static CQuat<I> aboutY(double angle) {
		return CQuat<I>(cos(angle / 2), 0.0, sin(angle / 2), 0.0);
	}

	static CQuat<I> aboutZ(double angle) {
		return CQuat<I>(cos(angle / 2), 0.0, 0.0, sin(angle / 2));
	}

	static CQuat<I> aboutZYX(CVector<> ang) {
		return QRotation<>::aboutZ(ang[0])
			 * QRotation<>::aboutY(ang[1])
			 * QRotation<>::aboutX(ang[2]);
	}

	static CQuat<I> aboutZXY(CVector<> ang) {
		return QRotation<>::aboutZ(ang[0])
			 * QRotation<>::aboutX(ang[1])
			 * QRotation<>::aboutY(ang[2]);
	}
};
#endif // Quat_h
