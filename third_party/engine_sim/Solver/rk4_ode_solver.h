#ifndef ATG_SIMPLE_2D_CONSTRAINT_SOLVER_RK4_ODE_SOLVER_H
#define ATG_SIMPLE_2D_CONSTRAINT_SOLVER_RK4_ODE_SOLVER_H

#include "ode_solver.h"

namespace atg_scs {
    class Rk4OdeSolver : public OdeSolver {
        public:
            enum class RkStage {
                Stage_1,
                Stage_2,
                Stage_3,
                Stage_4,
                Complete,
                Undefined
            };

        public:
            Rk4OdeSolver();
            virtual ~Rk4OdeSolver();

            virtual void start(SystemState *initial, double dt);
            virtual bool step(SystemState *system);
            virtual void solve(SystemState *system);
            virtual void end();

        protected:
            static RkStage getNextStage(RkStage stage);

        protected:
            RkStage m_stage;
            RkStage m_nextStage;

            SystemState m_initialState;
            SystemState m_accumulator;
    };
} /* namespace atg_scs */

#endif /* ATG_SIMPLE_2D_CONSTRAINT_SOLVER_ODE_SOLVER_H */
