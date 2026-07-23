#ifndef ATG_SIMPLE_2D_CONSTRAINT_SOLVER_SLE_SOLVER_H
#define ATG_SIMPLE_2D_CONSTRAINT_SOLVER_SLE_SOLVER_H

#include "matrix.h"
#include "sparse_matrix.h"

namespace atg_scs {
    class SleSolver {
        public:
            SleSolver(bool supportsLimits);
            virtual ~SleSolver();

            virtual bool solve(
                    SparseMatrix<3> &J,
                    Matrix &W,
                    Matrix &right,
                    Matrix *result,
                    Matrix *previous);
            virtual bool solveWithLimits(
                    SparseMatrix<3> &J,
                    Matrix &W,
                    Matrix &right,
                    Matrix &limits,
                    Matrix *result,
                    Matrix *previous);

            bool supportsLimits() const { return m_supportsLimits; }

        private:
            bool m_supportsLimits;
    };
} /* namespace atg_scs */

#endif /* ATG_SIMPLE_2D_CONSTRAINT_SOLVER_SLE_SOLVER_H */
