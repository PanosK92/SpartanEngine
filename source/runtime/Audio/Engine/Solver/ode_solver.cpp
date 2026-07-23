#include "ode_solver.h"

atg_scs::OdeSolver::OdeSolver() {
    m_dt = 0.0;
}

atg_scs::OdeSolver::~OdeSolver() {
    /* void */
}

void atg_scs::OdeSolver::start(SystemState *initial, double dt) {
    m_dt = dt;
}

bool atg_scs::OdeSolver::step(SystemState *system) {
    return true;
}

void atg_scs::OdeSolver::solve(SystemState *system) {
    /* void */
}

void atg_scs::OdeSolver::end() {
    /* void */
}
