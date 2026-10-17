//
// Matrix.h: A CMatrix class declaration & definition.
//			 2 parameter template-based CMatrix class
//			 A CRotrix class with static methods
//////////////////////////////////////////////////////

#ifndef Matrix_h
#define Matrix_h

#include "Vector.h"

template <size_t M = 3, size_t N = 3>
class CMatrix {
    CVector<N> matrix[M];

public:
    CMatrix<M, N>() { 
        CVector<N> v;
        for (size_t i = 0; i < M; i++) matrix[i] = v; 
    }

    CMatrix<M, N>(const double *u, const double *v, const double *w) {
        CVector<N> v1(u), v2(v), v3(w);
        matrix[0] = v1;
        if (M > 1) matrix[1] = v2;
        if (M > 2) matrix[2] = v3;
    }

	CMatrix<M, N>(const CVector<N> v1, const CVector<N> v2, const CVector<N> v3) {
		matrix[0] = v1;
		if (M > 1) matrix[1] = v2;
        if (M > 2) matrix[2] = v3;
    }
	
    template <size_t K>
    CMatrix<M, N>(const CVector<K>& v) {
        assert(K == 3 && (M == K || M == 4));
        for (size_t i = 1; i < M; i++) matrix[i][0] = -(matrix[0][i] = v[i - 1]);

        int sign = 1;
        for (size_t i = M - K; i < M - 1; i++) {
            for (size_t j = i + 1; j < M; j++) {
                matrix[i][j] = -(matrix[j][i] = sign * v[(2 * M - K) - (i + j)]);
                sign = -sign;
            }
        }
    }

    size_t GetRows() { return M; }
    size_t GetColumns() { return N; }
    CVector<N> &operator[](size_t index) { 
        assert(index < M); return matrix[index]; }
    const CVector<N> &operator[](size_t index) const { 
        assert(index < M); return matrix[index]; }

//	Scalar & Matrix Operations
    CMatrix<M, N> operator*(const double &value) const {
        CMatrix<M, N> result(*this);
        for (size_t i = 0; i < M; i++) {
            for (size_t j = 0; j < N; j++) {
                result[i][j] *= value;
            }
        }
        
        return result;
    }

    CMatrix<M, N> operator / (const double &value) const {
        assert(value != 0.0);
        CMatrix<M, N> result(*this);
        for (size_t i = 0; i < M; i++) {
            for (size_t j = 0; j < N; j++) {
                result[i][j] /= value;
            }
        }
        
        return result;
    }

    friend CMatrix<M, N> operator*(const double &f, const CMatrix<M, N> &rhs) {
        return rhs*f;
    }

    CMatrix<M, N> operator-() {
        CMatrix<M, N> result;
        for (size_t i = 0; i < M; i++) result.matrix[i] = -matrix[i];
    
        return result;
    }

//  Matrix & Vector Operation
    CVector<M> operator * (const CVector<N> &rhs) const {
        CVector<M> result;
        for (size_t i = 0; i < M; i++) {
            double sum = 0.0;
            for (size_t k = 0; k < N; k++)
                sum += matrix[i][k] * rhs[k];
            result[i] = sum;
        }

        return result;
    }

    CMatrix<M, N> operator + (const CMatrix<M, N> &rhs) const {
        CMatrix<M, N> result(*this);
        for (size_t i = 0; i < M; i++) {
                result[i] = matrix[i] + rhs[i];
        }

        return result;
    }

    CMatrix<M, N> operator - (const CMatrix<M, N> &rhs) const {
        CMatrix<M, N> result(*this);
        for (size_t i = 0; i < M; i++) {
                result[i] = matrix[i] - rhs[i];
        }

        return result;
    }

    template <size_t K>
    CMatrix<M, K> operator*(const CMatrix<N, K> &rhs) const {
        CMatrix<M, K> result;
        for (size_t i = 0; i < M; i++) {
            for (size_t j = 0; j < K; j++) {
                double sum = 0.0;
                for (size_t k = 0; k < N; k++) 
                    sum += matrix[i][k] * rhs[k][j];
                result[i][j] = sum;
            }
        }
        return result;
    }

    template <size_t K>
    CMatrix<M, K> operator ^ (const CMatrix<N, K> &rhs) const {
        CMatrix<M, K> result;
        assert(M == N); // Ensure matrix symmetry

        for (size_t i = 0; i < M; i++) {
            for (size_t j = i; j < K; j++) { // Enforce symmetry
                double sum = 0.0;
                for (size_t k = 0; k < N; k++) 
                    sum += matrix[i][k] * rhs[k][j];
                result[j][i] = result[i][j] = sum; // Sym assign
            }
        }

        return result;
    }

    CMatrix<M, N> operator~() {
        CMatrix<M, N> result;
        assert(M == N);  // Ensure square matrix for transposition
        for (size_t i = 0; i < M; i++) {
            for (size_t j = 0; j < N; j++) {
                result[j][i] = matrix[i][j];
            }
        }
        return result;
    }

    CMatrix<M, N> SetDiag(const CVector<M> &A) {
        assert(M == N);
        for (size_t i = 0; i < M; i++) {
            matrix[i][i] = A[i];
        }
		
        return *this;
    }
    // Alternate 2x2 Inverse Function - taking a value as default detereminant
    CMatrix<M, N> inverse(double default_det) {
        assert(M == 2 && M == N);
        CMatrix<M, N> result(*this);
        double det = result[0][0] * result[1][1] - result[0][1] * result[1][0];
        double temp = result[0][0];

        det = MAX(det, default_det);

        result[0][0] = result[1][1] / det;
        result[0][1] = -result[0][1] / det;
        result[1][0] = -result[1][0] / det;
        result[1][1] = temp / det;

        return result;
    }


    // Sub-matrix Extraction
    // Sample usage: { CMatrix<5, 5> P; CMatrix<3, 3> R = P.select<3, 3>(1, 1); }
    template <size_t J, size_t K>
    CMatrix<J, K> select(size_t m, size_t n) const {
        assert(m < M - J && n < N - K);
        CMatrix<J, K> result;
        for (size_t i = 0; i < J; i++)
            for (size_t j = 0; j < K; j++)
                result[i][j] = matrix[m + i][n + j];
        return result;
    }

    // Sub-matrix Substituition
    // Sample usage: { CMatrix<5, 5> P; CMatrix<3, 3> R = P.select<3, 3>(1, 1); }
    template <size_t J, size_t K>
    void update(size_t m, size_t n, CMatrix<J, K> rhs) {
        assert(m < M - J && n < N - K);
        for (size_t i = 0; i < J; i++)
            for (size_t j = 0; j < K; j++)
                matrix[m + i][n + j] = rhs[i][j];
    }

    // Display contents
#ifdef _STD_IO_
    void print(char *title = "") {
        printf("\n%s\n", title);
        for (size_t i = 0; i < M; i++) (*this)[i].printrow();
    }
#else
    friend std::ostream& operator<<(std::ostream& os, const CMatrix<M, N>& rhs) {
        for (size_t i = 0; i < M; ++i) {
            os << rhs[i];
        }
        return os << std::endl;
    }
#endif
};

template <size_t M = 3>
class CRotrix : public CMatrix<M, M> {

public:
    static CMatrix<M, M> eye() {
        CMatrix<M, M> result;
        for (size_t i = 0; i < M; i++)
            result[i][i] = 1.0;
        return result;
    }

    static CMatrix<M, M> aboutX(double angle) {
        CMatrix<M, M> result = eye();
        result[1][1] = result[2][2] = cos(angle);
        result[2][1] = -(result[1][2] = sin(angle));
        return result;
    }

    static CMatrix<M, M> aboutY(double angle) {
        CMatrix<M, M> result = eye();
        result[0][0] = result[2][2] = cos(angle);
        result[0][2] = -(result[2][0] = sin(angle));
        return result;
    }

    static CMatrix<M, M> aboutZ(double angle) {
        CMatrix<M, M> result = eye();
        result[0][0] = result[1][1] = cos(angle);
        result[1][0] = -(result[0][1] = sin(angle));
        return result;
    }
};
#endif // Matrix_h
