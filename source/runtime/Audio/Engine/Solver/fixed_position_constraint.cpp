#include "fixed_position_constraint.h"

#include <cmath>

atg_scs::FixedPositionConstraint::FixedPositionConstraint() : Constraint(2, 1) {
    m_local_x = m_local_y = 0.0;
    m_world_x = m_world_y = 0.0;
    m_ks = 10.0;
    m_kd = 1.0;
}

atg_scs::FixedPositionConstraint::~FixedPositionConstraint() {
    /* void */
}

void atg_scs::FixedPositionConstraint::setWorldPosition(double x, double y) {
    m_world_x = x;
    m_world_y = y;
}

void atg_scs::FixedPositionConstraint::setLocalPosition(double x, double y) {
    m_local_x = x;
    m_local_y = y;
}

void atg_scs::FixedPositionConstraint::calculate(
        Output *output,
        SystemState *state)
{
    const int body = m_bodies[0]->index;

    const double q1 = state->p_x[body];
    const double q2 = state->p_y[body];
    const double q3 = state->theta[body];

    const double q3_dot = state->v_theta[body];

    const double cos_q3 = std::cos(q3);
    const double sin_q3 = std::sin(q3);

    const double current_x = q1 + cos_q3 * m_local_x - sin_q3 * m_local_y;
    const double current_y = q2 + sin_q3 * m_local_x + cos_q3 * m_local_y;

    const double dx_dq1 = 1.0;
    const double dx_dq2 = 0.0;
    const double dx_dq3 = -sin_q3 * m_local_x - cos_q3 * m_local_y;

    const double dy_dq1 = 0.0;
    const double dy_dq2 = 1.0;
    const double dy_dq3 = cos_q3 * m_local_x - sin_q3 * m_local_y;

    const double C1 = current_x - m_world_x;
    const double C2 = current_y - m_world_y;

    output->J[0][0] = dx_dq1;
    output->J[0][1] = dx_dq2;
    output->J[0][2] = dx_dq3;

    output->J[1][0] = dy_dq1;
    output->J[1][1] = dy_dq2;
    output->J[1][2] = dy_dq3;

    output->J_dot[0][0] = 0;
    output->J_dot[0][1] = 0;
    output->J_dot[0][2] =
        -cos_q3 * q3_dot * m_local_x + sin_q3 * q3_dot * m_local_y;

    output->J_dot[1][0] = 0;
    output->J_dot[1][1] = 0;
    output->J_dot[1][2] =
        -sin_q3 * q3_dot * m_local_x - cos_q3 * q3_dot * m_local_y;

    output->ks[0] = m_ks;
    output->ks[1] = m_ks;

    output->kd[0] = output->kd[1] = m_kd;

    output->C[0] = C1;
    output->C[1] = C2;

    output->v_bias[0] = 0;
    output->v_bias[1] = 0;

    noLimits(output);
}
