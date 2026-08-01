#ifndef ATG_SIMPLE_2D_CONSTRAINT_SOLVER_CONSTANT_ROTATION_CONSTRAINT_H
#define ATG_SIMPLE_2D_CONSTRAINT_SOLVER_CONSTANT_ROTATION_CONSTRAINT_H

#include "constraint.h"

namespace atg_scs {
    class ConstantRotationConstraint : public Constraint {
        public:
            ConstantRotationConstraint();
            virtual ~ConstantRotationConstraint();

            void setBody(RigidBody *body) { m_bodies[0] = body; }

            virtual void calculate(Output *output, SystemState *state);

            double m_rotationSpeed;
            double m_ks;
            double m_kd;

            double m_minTorque;
            double m_maxTorque;
    };
} /* namespace atg_scs */

#endif /* ATG_SIMPLE_2D_CONSTRAINT_SOLVER_CONSTANT_ROTATION_CONSTRAINT_H */
