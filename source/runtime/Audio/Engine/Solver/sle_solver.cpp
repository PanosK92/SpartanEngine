#include "sle_solver.h"

atg_scs::SleSolver::SleSolver(bool supportsLimits) {
    m_supportsLimits = supportsLimits;
}

atg_scs::SleSolver::~SleSolver() {
    /* void */
}

bool atg_scs::SleSolver::solve(
        SparseMatrix<3> &J,
        Matrix &W,
        Matrix &right,
        Matrix *result,
        Matrix *previous)
{
    return false;
}

bool atg_scs::SleSolver::solveWithLimits(
        SparseMatrix<3> &J,
        Matrix &W,
        Matrix &right,
        Matrix &limits,
        Matrix *result,
        Matrix *previous)
{
    return false;
}
