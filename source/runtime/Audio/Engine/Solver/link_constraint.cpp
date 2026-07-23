#include "link_constraint.h"

#include <cmath>

atg_scs::LinkConstraint::LinkConstraint() : Constraint(2, 2) {
    m_local_x_1 = m_local_y_1 = 0.0;
    m_local_x_2 = m_local_y_2 = 0.0;
    m_ks = 10.0;
    m_kd = 1.0;
}

atg_scs::LinkConstraint::~LinkConstraint() {
    /* void */
}

void atg_scs::LinkConstraint::calculate(
        Output *output,
        SystemState *state)
{
    const int body = m_bodies[0]->index;
    const int linkedBody = m_bodies[1]->index;

    const double q1 = state->p_x[body];
    const double q2 = state->p_y[body];
    const double q3 = state->theta[body];

    const double q4 = state->p_x[linkedBody];
    const double q5 = state->p_y[linkedBody];
    const double q6 = state->theta[linkedBody];

    const double q3_dot = state->v_theta[body];
    const double q6_dot = state->v_theta[linkedBody];

    const double cos_q3 = std::cos(q3);
    const double sin_q3 = std::sin(q3);

    const double cos_q6 = std::cos(q6);
    const double sin_q6 = std::sin(q6);

    const double bodyX = q1 + cos_q3 * m_local_x_1 - sin_q3 * m_local_y_1;
    const double bodyY = q2 + sin_q3 * m_local_x_1 + cos_q3 * m_local_y_1;

    const double linkedBodyX = q4 + cos_q6 * m_local_x_2 - sin_q6 * m_local_y_2;
    const double linkedBodyY = q5 + sin_q6 * m_local_x_2 + cos_q6 * m_local_y_2;

    const double C1 = bodyX - linkedBodyX;
    const double C2 = bodyY - linkedBodyY;

    output->J[0][0] = 1.0;
    output->J[0][1] = 0.0;
    output->J[0][2] = -sin_q3 * m_local_x_1 - cos_q3 * m_local_y_1;

    output->J[1][0] = 0.0;
    output->J[1][1] = 1.0;
    output->J[1][2] = cos_q3 * m_local_x_1 - sin_q3 * m_local_y_1;

    output->J[0][3] = -1.0;
    output->J[0][4] = 0.0;
    output->J[0][5] = sin_q6 * m_local_x_2 + cos_q6 * m_local_y_2;

    output->J[1][3] = 0.0;
    output->J[1][4] = -1.0;
    output->J[1][5] = -cos_q6 * m_local_x_2 + sin_q6 * m_local_y_2;

    output->J_dot[0][0] = 0;
    output->J_dot[0][1] = 0;
    output->J_dot[0][2] =
        -cos_q3 * q3_dot * m_local_x_1 + sin_q3 * q3_dot * m_local_y_1;

    output->J_dot[1][0] = 0;
    output->J_dot[1][1] = 0;
    output->J_dot[1][2] =
        -sin_q3 * q3_dot * m_local_x_1 - cos_q3 * q3_dot * m_local_y_1;

    output->J_dot[0][3] = 0;
    output->J_dot[0][4] = 0;
    output->J_dot[0][5] =
        cos_q6 * q6_dot * m_local_x_2 - sin_q6 * q6_dot * m_local_y_2;

    output->J_dot[1][3] = 0;
    output->J_dot[1][4] = 0;
    output->J_dot[1][5] =
        sin_q6 * q6_dot * m_local_x_2 + cos_q6 * q6_dot * m_local_y_2;

    output->kd[0] = output->kd[1] = m_kd;
    output->ks[0] = m_ks;
    output->ks[1] = m_ks;

    output->C[0] = C1;
    output->C[1] = C2;

    output->v_bias[0] = 0;
    output->v_bias[1] = 0;

    noLimits(output);
}

void atg_scs::LinkConstraint::setLocalPosition1(double x, double y) {
    m_local_x_1 = x;
    m_local_y_1 = y;
}

void atg_scs::LinkConstraint::setLocalPosition2(double x, double y) {
    m_local_x_2 = x;
    m_local_y_2 = y;
}
