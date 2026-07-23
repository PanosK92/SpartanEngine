#ifndef ATG_SIMPLE_2D_CONSTRAINT_SOLVER_FORCE_GENERATOR_H
#define ATG_SIMPLE_2D_CONSTRAINT_SOLVER_FORCE_GENERATOR_H

#include "system_state.h"

namespace atg_scs {
    class ForceGenerator {
        public:
            ForceGenerator();
            virtual ~ForceGenerator();

            virtual void apply(SystemState *system) = 0;

            int m_index;
    };
} /* namespace atg_scs */

#endif /* ATG_SIMPLE_2D_CONSTRAINT_SOLVER_FORCE_GENERATOR_H */
