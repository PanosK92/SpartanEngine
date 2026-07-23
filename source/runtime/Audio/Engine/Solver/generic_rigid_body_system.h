#ifndef ATG_SIMPLE_2D_CONSTRAINT_SOLVER_GENERIC_RIGID_BODY_SYSTEM_H
#define ATG_SIMPLE_2D_CONSTRAINT_SOLVER_GENERIC_RIGID_BODY_SYSTEM_H

#include "rigid_body_system.h"

#include "sle_solver.h"
#include "ode_solver.h"

namespace atg_scs {
    class GenericRigidBodySystem : public RigidBodySystem {
        public:
            GenericRigidBodySystem();
            virtual ~GenericRigidBodySystem();

            void initialize(SleSolver *sleSolver, OdeSolver *odeSolver);
            virtual void process(double dt, int steps = 1);

        protected:
            void processConstraints(
                    long long *evalTime,
                    long long *solveTime);

        protected:
            OdeSolver *m_odeSolver;
            SleSolver *m_sleSolver;

        protected:
            struct IntermediateValues {
                SparseMatrix<3> J_sparse, J_dot_sparse, sreg0;
                Matrix J_T;
                Matrix M, M_inv;
                Matrix C;
                Matrix ks, kd;
                Matrix q_dot;

                Matrix reg0, reg1, reg2;

                Matrix right;
                Matrix F_ext, F_C, R;

                // Results
                Matrix lambda;
            } m_iv;
    };
} /* namespace atg_scs */

#endif /* ATG_SIMPLE_2D_CONSTRAINT_SOLVER_GENERIC_RIGID_BODY_SYSTEM_H */
