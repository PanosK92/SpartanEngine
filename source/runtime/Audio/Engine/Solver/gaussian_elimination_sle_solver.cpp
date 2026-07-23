#include "gaussian_elimination_sle_solver.h"

#include <cmath>
#include <assert.h>
#include <fstream>

atg_scs::GaussianEliminationSleSolver::GaussianEliminationSleSolver()
    : atg_scs::SleSolver(false)
{
    m_a.initialize(1, 1);
    m_M.initialize(1, 1);
}

atg_scs::GaussianEliminationSleSolver::~GaussianEliminationSleSolver() {
    m_a.destroy();
    m_M.destroy();
    m_reg.destroy();
}

bool atg_scs::GaussianEliminationSleSolver::solve(
        SparseMatrix<3> &J,
        Matrix &W,
        Matrix &right,
        Matrix *previous,
        Matrix *result)
{
    J.rightScale(W, &m_reg);
    m_reg.multiplyTranspose(J, &m_M);

    const int n = m_M.getWidth() + 1;
    const int m = m_M.getHeight();

    assert(right.getHeight() == m_M.getWidth());

    if (n == 0 || m == 0) return true;

    result->resize(1, m);

    Matrix &A = m_a;
    A.resize(n, m);

    for (int i = 0; i < m; ++i) {
        for (int j = 0; j < n - 1; ++j) {
            A.set(j, i, m_M.get(j, i));
        }
        A.set(n - 1, i, right.get(0, i));
    }

    int h = 0, k = 0;
    while (h < m && k < n) {
        int i_max = h;
        double maxV = fastAbs(A.get(k, i_max));
        for (int i = h + 1; i < m; ++i) {
            const double v = fastAbs(A.get(k, i));
            if (v > maxV) {
                maxV = v;
                i_max = i;
            }
        }

        if (maxV == 0) {
            ++k;
        }
        else {
            A.fastRowSwap(h, i_max);

            for (int i = h + 1; i < m; ++i) {
                const double f = A.get(k, i) / A.get(k, h);
                A.set(k, i, 0.0);

                for (int j = k + 1; j < n; ++j) {
                    A.set(j, i, A.get(j, i) - A.get(j, h) * f);
                }
            }

            ++h;
            ++k;
        }
    }

    if (A.get(n - 2, m - 1) == 0) {
        assert(false);
    }

    const double x_m = A.get(n - 1, m - 1) / A.get(n - 2, m - 1);
    result->set(0, m - 1, x_m);
    for (int i = m - 2; i >= 0; --i) {
        const double b_i = A.get(n - 1, i);
        double sum = 0.0;
        for (int j = m - 1; j > i; --j) {
            sum += A.get(j, i) * result->get(0, j);
        }

        if (A.get(i, i) != 0) {
            result->set(0, i, (b_i - sum) / A.get(i, i));
        }
        else {
            result->set(0, i, 0);
        }
    }

    for (int i = 0; i < m; ++i) {
        if (std::isnan(result->get(0, i)) || std::isinf(result->get(0, i))) {
            assert(false);
        }
    }

    return true;
}
