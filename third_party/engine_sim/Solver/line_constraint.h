#ifndef ATG_SIMPLE_2D_CONSTRAINT_SOLVER_LINE_CONSTRAINT_H
#define ATG_SIMPLE_2D_CONSTRAINT_SOLVER_LINE_CONSTRAINT_H

#include "constraint.h"

namespace atg_scs {
    class LineConstraint : public Constraint {
        public:
            LineConstraint();
            virtual ~LineConstraint();
            
            void setBody(RigidBody *body) { m_bodies[0] = body; }

            virtual void calculate(Output *output, SystemState *system);

            double m_local_x;
            double m_local_y;
            double m_p0_x;
            double m_p0_y;
            double m_dx;
            double m_dy;

            double m_ks;
            double m_kd;
    };
} /* namespace atg_scs */

#endif /* ATG_SIMPLE_2D_CONSTRAINT_SOLVER_LINE_CONSTRAINT_H */
