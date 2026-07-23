#include "rigid_body.h"

#include <cmath>

atg_scs::RigidBody::RigidBody() {
    index = -1;
    reset();
}

atg_scs::RigidBody::~RigidBody() {
    /* void */
}

double atg_scs::RigidBody::energy() const {
    const double speed_2 = v_x * v_x + v_y * v_y;
    const double E_k = 0.5 * m * speed_2;
    const double E_r = 0.5 * I * v_theta * v_theta;

    return E_k + E_r;
}

void atg_scs::RigidBody::localToWorld(
        double x,
        double y,
        double *w_x,
        double *w_y)
{
    const double cos_theta = std::cos(theta);
    const double sin_theta = std::sin(theta);

    *w_x = cos_theta * x - sin_theta * y + p_x;
    *w_y = sin_theta * x + cos_theta * y + p_y;
}

void atg_scs::RigidBody::worldToLocal(
        double x,
        double y,
        double *l_x,
        double *l_y)
{
    const double cos_theta = std::cos(theta);
    const double sin_theta = std::sin(theta);

    *l_x = cos_theta * (x - p_x) + sin_theta * (y - p_y);
    *l_y = -sin_theta * (x - p_x) + cos_theta * (y - p_y);
}

void atg_scs::RigidBody::reset() {
    p_x = p_y = 0.0;
    v_x = v_y = 0.0;

    theta = 0.0;
    v_theta = 0.0;

    m = 0.0;
    I = 0.0;
}
