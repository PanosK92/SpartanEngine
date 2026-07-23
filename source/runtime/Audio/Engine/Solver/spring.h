#ifndef ATG_SIMPLE_2D_CONSTRAINT_SOLVER_SPRING_H
#define ATG_SIMPLE_2D_CONSTRAINT_SOLVER_SPRING_H

#include "force_generator.h"

#include "rigid_body.h"

namespace atg_scs {
    class Spring : public ForceGenerator {
        public:
            Spring();
            virtual ~Spring();

            virtual void apply(SystemState *state);
            
            void getEnds(double *x_1, double *y_1, double *x_2, double *y_2);
            double energy() const;

            double m_restLength;
            double m_ks;
            double m_kd;

            double m_p1_x;
            double m_p1_y;

            double m_p2_x;
            double m_p2_y;

            RigidBody *m_body1;
            RigidBody *m_body2;
    };
} /* namespace atg_scs */

#endif /* ATG_SIMPLE_2D_CONSTRAINT_SOLVER_SPRING_H */
