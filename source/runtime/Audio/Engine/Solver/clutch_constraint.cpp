#include "clutch_constraint.h"

#include <cmath>
#include <cfloat>

atg_scs::ClutchConstraint::ClutchConstraint() : Constraint(1, 2) {
    m_ks = 10.0;
    m_kd = 1.0;

    m_maxTorque = DBL_MAX;
    m_minTorque = -DBL_MAX;
}

atg_scs::ClutchConstraint::~ClutchConstraint() {
    /* void */
}

void atg_scs::ClutchConstraint::calculate(
        Output *output,
        SystemState *state)
{
    output->C[0] = 0;

    output->J[0][0] = 0.0;
    output->J[0][1] = 0.0;
    output->J[0][2] = -1.0;

    output->J[0][3] = 0.0;
    output->J[0][4] = 0.0;
    output->J[0][5] = 1.0;

    output->J_dot[0][0] = 0;
    output->J_dot[0][1] = 0;
    output->J_dot[0][2] = 0;

    output->J_dot[0][3] = 0;
    output->J_dot[0][4] = 0;
    output->J_dot[0][5] = 0;

    output->kd[0] = m_kd;
    output->ks[0] = m_ks;

    output->v_bias[0] = 0;

    output->limits[0][0] = m_minTorque;
    output->limits[0][1] = m_maxTorque;
}
