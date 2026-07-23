#ifndef ATG_SIMPLE_2D_CONSTRAINT_SOLVER_OPTIMIZED_NSV_RIGID_BODY_SYSTEM_H
#define ATG_SIMPLE_2D_CONSTRAINT_SOLVER_OPTIMIZED_NSV_RIGID_BODY_SYSTEM_H

#include "rigid_body_system.h"

#include "sle_solver.h"
#include "nsv_ode_solver.h"

namespace atg_scs {
    class OptimizedNsvRigidBodySystem : public RigidBodySystem {
        public:
            OptimizedNsvRigidBodySystem();
            virtual ~OptimizedNsvRigidBodySystem();

            void initialize(SleSolver *sleSolver);
            virtual void process(double dt, int steps = 1);

            double m_biasFactor;

        protected:
            void propagateResults();
            void processConstraints(
                    double dt,
                    long long *evalTime,
                    long long *solveTime);

        protected:
            NsvOdeSolver m_odeSolver;
            SleSolver *m_sleSolver;

        protected:
            struct IntermediateValues {
                SparseMatrix<3> J_sparse, sreg0;
                Matrix C;
                Matrix M, M_inv;
                Matrix b_err, v_bias;
                Matrix limits;

                Matrix q_dot, q_dot_prime;

                Matrix reg0, reg1, reg2;

                Matrix right;
                Matrix F_ext, F_C, R;

                // Results
                Matrix lambda;
            } m_iv;
    };
} /* namespace atg_scs */

#endif /* ATG_SIMPLE_2D_CONSTRAINT_SOLVER_OPTIMIZED_NSV_RIGID_BODY_SYSTEM_H */
