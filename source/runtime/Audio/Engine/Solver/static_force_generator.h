#ifndef ATG_SIMPLE_2D_CONSTRAINT_SOLVER_STATIC_FORCE_GENERATOR_H
#define ATG_SIMPLE_2D_CONSTRAINT_SOLVER_STATIC_FORCE_GENERATOR_H

#include "force_generator.h"

#include "rigid_body.h"

namespace atg_scs {
    class StaticForceGenerator : public ForceGenerator {
        public:
            StaticForceGenerator();
            virtual ~StaticForceGenerator();

            virtual void apply(SystemState *state);

            void setForce(double f_x, double f_y);
            void setPosition(double p_x, double p_y);

            double m_f_x;
            double m_f_y;

            double m_p_x;
            double m_p_y;

            RigidBody *m_body;
    };
} /* namespace atg_scs */

#endif /* ATG_SIMPLE_2D_CONSTRAINT_SOLVER_STATIC_FORCE_GENERATOR_H */
