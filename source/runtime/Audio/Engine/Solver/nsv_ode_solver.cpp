#include "nsv_ode_solver.h"

atg_scs::NsvOdeSolver::NsvOdeSolver() {
    /* void */
}

atg_scs::NsvOdeSolver::~NsvOdeSolver() {
    /* void */
}

void atg_scs::NsvOdeSolver::start(SystemState *initial, double dt) {
    OdeSolver::start(initial, dt);
}

bool atg_scs::NsvOdeSolver::step(SystemState *system) {
    system->dt = m_dt;
    return true;
}

void atg_scs::NsvOdeSolver::solve(SystemState *system) {
    system->dt = m_dt;

    for (int i = 0; i < system->n; ++i) {
        system->v_x[i] += system->a_x[i] * m_dt;
        system->v_y[i] += system->a_y[i] * m_dt;
        system->v_theta[i] += system->a_theta[i] * m_dt;

        system->p_x[i] += system->v_x[i] * m_dt;
        system->p_y[i] += system->v_y[i] * m_dt;
        system->theta[i] += system->v_theta[i] * m_dt;
    }
}

void atg_scs::NsvOdeSolver::end() {
    /* void */
}
