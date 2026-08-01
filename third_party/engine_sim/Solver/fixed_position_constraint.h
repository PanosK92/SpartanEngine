#ifndef ATG_SIMPLE_2D_CONSTRAINT_SOLVER_FIXED_POSITION_CONSTRAINT_H
#define ATG_SIMPLE_2D_CONSTRAINT_SOLVER_FIXED_POSITION_CONSTRAINT_H

#include "constraint.h"

namespace atg_scs {
    class FixedPositionConstraint : public Constraint {
        public:
            FixedPositionConstraint();
            virtual ~FixedPositionConstraint();
            
            void setBody(RigidBody *body) { m_bodies[0] = body; }

            void setWorldPosition(double x, double y);
            void setLocalPosition(double x, double y);

            virtual void calculate(Output *output, SystemState *system);

            double m_local_x;
            double m_local_y;
            double m_world_x;
            double m_world_y;
            double m_ks;
            double m_kd;
    };
} /* namespace atg_scs */

#endif /* ATG_SIMPLE_2D_CONSTRAINT_SOLVER_FIXED_POSITION_CONSTRAINT_H */
