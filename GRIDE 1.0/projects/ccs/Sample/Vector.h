//
// Vector.h: A CVector class declaration & definition.
//			 1 parameter template-based CVector class
//////////////////////////////////////////////////////
#ifndef Vector_h
#define Vector_h

#include <math.h>
#include <string.h>
#include <assert.h>

#ifdef _STD_IO_
#include <stdio.h>
#else
#include <iostream>
//using namespace std;
#endif

#define	MAX(A, B)	((A) > (B) ? (A) : (B))
#define	MIN(A, B)	((A) < (B) ? (A) : (B))

// Vector is a Matrix of M x 1  
template<size_t M, size_t N> class CMatrix;

template<size_t N = 3>
class CVector {
protected:
	double vector[N];

public:
	CVector<N>(double *v = 0) {
		if (!v) memset(vector, 0, N*sizeof(double));
		else memcpy(vector, v, N*sizeof(double));
	}

    CVector<N>(double v1, double v2) {
        vector[0] = v1;
        vector[1] = v2;
    }

	CVector<N>(double v1, double v2, double v3) {
		vector[0] = v1;
		vector[1] = v2;
		vector[2] = v3;
	}

	CVector<N>(double v1, double v2, double v3, double v4) {
		vector[0] = v1;
		vector[1] = v2;
		vector[2] = v3;
		vector[3] = v4;
	}

	CVector<N>& operator = (const double *rhs) {
		memcpy(vector, rhs, N*sizeof(double));
		return *this; 
	}

	double &operator [] (size_t index) {
		assert(index < N);
		return vector[index];
	}

	const double &operator [] (size_t index) const {
		assert(index < N);
		return vector[index];
	}

	size_t GetOrder() const {
		return N;
	}

//	Scalar Operation
	CVector<N> operator * (const double &value) const {
		CVector<N> Temp(*this);
		for (size_t i = 0; i < N; i++) Temp[i] *= value;
		return Temp;
	}

	friend  CVector<N> operator * (const double &value, const CVector<N> &rhs) {
		return rhs*value;
	}

	CVector<N> operator / (const double &value) const {
		CVector<N> Temp(*this);
		if (value != double(0)) {
			for (size_t i = 0; i < N; i++) Temp[i] /= value;
		}

		return Temp;
	}

//  Vector Operations
	CVector<N> operator-() const {
		CVector<N> result;
		for (size_t i = 0; i < N; i++)
			result.vector[i] = -vector[i];

		return result;
	}

	CVector<N> operator + (const CVector<N> &rhs) const {
		CVector<N> result(*this);
		for (size_t i = 0; i < N; i++)
			result[i] += rhs.vector[i];

		return result;
	}

	CVector<N> operator - (const CVector<N> &rhs) const {

		CVector<N> result(*this);
		for (size_t i = 0; i < N; i++)
			result[i] -= rhs.vector[i];

		return result;
	}

	void operator += (const CVector<N> &rhs) {

		for (size_t i = 0; i < N; i++)
			vector[i] += rhs.vector[i];
	}

	void operator -= (const CVector<N> &rhs) {

		for (size_t i = 0; i < N; i++)
			vector[i] -= rhs.vector[i];
	}

//  Optimized Quaternion Multiplication
	template<size_t K>
	CVector<K> operator*(const CVector<K> &rhs) const {
		CVector<N> lhs(*this);
		assert(N == 3 || N == 4);
		if (N == 4) { // for quaternion
			double w1 = lhs[0], x1 = lhs[1], y1 = lhs[2], z1 = lhs[3];
			double w2 = rhs[0], x2 = rhs[1], y2 = rhs[2], z2 = rhs[3];
			return CVector<K>(
				w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
				w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
				w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
				w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2);
		}
        //if (N == 3) { // cross product
        return (CMatrix<K, K>)lhs * rhs;
        //}
	}

	// Dot product
	double operator % (const CVector<N> &rhs) const { // dot product
		double result = 0.0;
		for (size_t i = 0; i < N; i++) result += vector[i] * rhs[i];
		return result;
	}
	// Component multiply operator
	CVector<N> operator ^ (const CVector<N> &rhs) const { // component product
		return CVector<N>(vector[0] * rhs[0], vector[1] * rhs[1], vector[2] * rhs[2]);
	}

	// Concat operator
	template<size_t K>
	CVector<N + K> operator | (const CVector<K> &rhs) const {
		CVector<N + K> result;
		for (size_t i = 0; i < N; i++) result[i] = vector[i];
		for (size_t j = 0; j < K; j++) result[N + j] = rhs[j];
		return result;
	}

	double mag2() {
		double norm = 0.0;
		for (size_t i = 0; i < N; i++) norm += SQR(vector[i]);
		return norm;
	}

	double mag() { 
		double mg2 = mag2();
		assert(mg2 >= 0.0);
		return sqrt(mg2); 
	}

	void norm() {
		double norm = mag();
		assert(norm != 0.0);
		if (norm != 0.0) {
			for (size_t j = 0; j < N; j++) vector[j] /= norm;
		}
	}

	// Sub-vector Extraction
	// Sample usage: { CVector<5> V; CVector<3> Y = V.select<3>(2); }
	template <size_t K>
	CVector<K> select(size_t n) const {
		CVector<K> result;
		//assert(0 <= n && n <= N - K);
		for (size_t i = 0; i < K; i++)
			result[i] = vector[n + i];

		return result;
	}

#ifdef _STD_IO_
	void print(char *title = "") {
		printf("\n%s ", title);
		for (size_t i = 0; i < N; i++) printf("\t%13.12lf", this->vector[i]);
	}
	void printrow() {
		printf("%13.12lf", this->vector[0]);
		for (size_t i = 1; i < N; i++) printf("\t%13.12lf", this->vector[i]);
		printf("\n");
	}
#else
    friend std::ostream& operator << (std::ostream& os, const CVector<N> &rhs) {
        for(size_t i = 0; i < N; i++) os << " " << rhs.vector[i];
        return os << std::endl;
    }
#endif
};
#endif // Vector_h
