#ifndef ATG_SIMPLE_2D_CONSTRAINT_SOLVER_LINK_CONSTRAINT_H
#define ATG_SIMPLE_2D_CONSTRAINT_SOLVER_LINK_CONSTRAINT_H

#include "constraint.h"

namespace atg_scs {
    class LinkConstraint : public Constraint {
        public:
            LinkConstraint();
            virtual ~LinkConstraint();
            
            void setBody1(RigidBody *body) { m_bodies[0] = body; }
            void setBody2(RigidBody *body) { m_bodies[1] = body; }

            void setLocalPosition1(double x, double y);
            void setLocalPosition2(double x, double y);

            virtual void calculate(Output *output, SystemState *system);

            double m_local_x_1;
            double m_local_y_1;
            double m_local_x_2;
            double m_local_y_2;
            double m_ks;
            double m_kd;
    };
} /* namespace atg_scs */

#endif /* ATG_SIMPLE_2D_CONSTRAINT_SOLVER_LINK_CONSTRAINT_H */
