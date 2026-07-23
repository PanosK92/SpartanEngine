#include "rotation_friction_constraint.h"

#include <cfloat>

atg_scs::RotationFrictionConstraint::RotationFrictionConstraint() : Constraint(1, 1) {
    m_ks = 10.0;
    m_kd = 1.0;

    m_maxTorque = DBL_MAX;
    m_minTorque = -DBL_MAX;
}

atg_scs::RotationFrictionConstraint::~RotationFrictionConstraint() {
    /* void */
}

void atg_scs::RotationFrictionConstraint::calculate(
        Output *output,
        SystemState *state)
{
    const int body = m_bodies[0]->index;

    output->C[0] = 0;

    output->J[0][0] = 0.0;
    output->J[0][1] = 0.0;
    output->J[0][2] = 1.0;

    output->J_dot[0][0] = 0;
    output->J_dot[0][1] = 0;
    output->J_dot[0][2] = 0;

    output->kd[0] = m_kd;
    output->ks[0] = m_ks;

    output->v_bias[0] = 0;

    output->limits[0][0] = m_minTorque;
    output->limits[0][1] = m_maxTorque;
}
