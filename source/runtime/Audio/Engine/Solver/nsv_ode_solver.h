#ifndef ATG_SIMPLE_2D_CONSTRAINT_SOLVER_NSV_ODE_SOLVER_H
#define ATG_SIMPLE_2D_CONSTRAINT_SOLVER_NSV_ODE_SOLVER_H

#include "ode_solver.h"

namespace atg_scs {
    class NsvOdeSolver : public OdeSolver {
        public:
            NsvOdeSolver();
            virtual ~NsvOdeSolver();

            virtual void start(SystemState *initial, double dt);
            virtual bool step(SystemState *system);
            virtual void solve(SystemState *system);
            virtual void end();
    };
} /* namespace atg_scs */

#endif /* ATG_SIMPLE_2D_CONSTRAINT_SOLVER_NSV_ODE_SOLVER_H */
