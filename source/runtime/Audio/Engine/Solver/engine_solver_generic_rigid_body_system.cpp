#include "pch.h"
#include "generic_rigid_body_system.h"

#include <chrono>

atg_scs::GenericRigidBodySystem::GenericRigidBodySystem() {
    m_sleSolver = nullptr;
    m_odeSolver = nullptr;
}

atg_scs::GenericRigidBodySystem::~GenericRigidBodySystem() {
    /* void */
}

void atg_scs::GenericRigidBodySystem::initialize(
        SleSolver *sleSolver,
        OdeSolver *odeSolver)
{
    m_sleSolver = sleSolver;
    m_odeSolver = odeSolver;

    m_iv.lambda.initialize(1, 1);
}

void atg_scs::GenericRigidBodySystem::process(double dt, int steps) {
    long long
        odeSolveTime = 0,
        constraintSolveTime = 0,
        forceEvalTime = 0,
        constraintEvalTime = 0;

    populateSystemState();
    populateMassMatrices(&m_iv.M, &m_iv.M_inv);

    for (int i = 0; i < steps; ++i) {
        m_odeSolver->start(&m_state, dt / steps);

        while (true) {
            const bool done = m_odeSolver->step(&m_state);

            long long evalTime = 0, solveTime = 0;

            auto s0 = std::chrono::steady_clock::now();
            processForces();
            auto s1 = std::chrono::steady_clock::now();

            processConstraints(&evalTime, &solveTime);

            auto s2 = std::chrono::steady_clock::now();
            m_odeSolver->solve(&m_state);
            auto s3 = std::chrono::steady_clock::now();

            constraintSolveTime += solveTime;
            constraintEvalTime += evalTime;
            odeSolveTime +=
                std::chrono::duration_cast<std::chrono::microseconds>(s3 - s2).count();
            forceEvalTime +=
                std::chrono::duration_cast<std::chrono::microseconds>(s1 - s0).count();

            if (done) break;
        }

        m_odeSolver->end();
    }

    const int n = getRigidBodyCount();
    for (int i = 0; i < n; ++i) {
        m_rigidBodies[i]->v_x = m_state.v_x[i];
        m_rigidBodies[i]->v_y = m_state.v_y[i];

        m_rigidBodies[i]->p_x = m_state.p_x[i];
        m_rigidBodies[i]->p_y = m_state.p_y[i];

        m_rigidBodies[i]->v_theta = m_state.v_theta[i];
        m_rigidBodies[i]->theta = m_state.theta[i];
    }

    const int m = getConstraintCount();
    for (int i = 0, i_f = 0; i < m; ++i) {
        Constraint *constraint = m_constraints[i];

        for (int j = 0; j < constraint->getConstraintCount(); ++j, ++i_f) {
            for (int k = 0; k < constraint->m_bodyCount; ++k) {
                constraint->F_x[j][k] = m_state.r_x[i_f];
                constraint->F_y[j][k] = m_state.r_y[i_f];
                constraint->F_t[j][k] = m_state.r_t[i_f];
            }
        }
    }

    m_odeSolveMicroseconds[m_frameIndex] = odeSolveTime;
    m_constraintSolveMicroseconds[m_frameIndex] = constraintSolveTime;
    m_forceEvalMicroseconds[m_frameIndex] = forceEvalTime;
    m_constraintEvalMicroseconds[m_frameIndex] = constraintEvalTime;
    m_frameIndex = (m_frameIndex + 1) % ProfilingSamples;
}

void atg_scs::GenericRigidBodySystem::processConstraints(
        long long *evalTime,
        long long *solveTime)
{
    *evalTime = -1;
    *solveTime = -1;

    auto s0 = std::chrono::steady_clock::now();

    const int n = getRigidBodyCount();
    const int m_f = getFullConstraintCount();
    const int m = getConstraintCount();

    m_iv.q_dot.resize(1, n * 3);
    for (int i = 0; i < n; ++i) {
        m_iv.q_dot.set(0, i * 3 + 0, m_state.v_x[i]);
        m_iv.q_dot.set(0, i * 3 + 1, m_state.v_y[i]);
        m_iv.q_dot.set(0, i * 3 + 2, m_state.v_theta[i]);
    }

    m_iv.J_sparse.initialize(3 * n, m_f);
    m_iv.J_dot_sparse.initialize(3 * n, m_f);
    m_iv.ks.initialize(1, m_f);
    m_iv.kd.initialize(1, m_f);
    m_iv.C.initialize(1, m_f);

    Constraint::Output constraintOutput;
    for (int j = 0, j_f = 0; j < m; ++j) {
        m_constraints[j]->calculate(&constraintOutput, &m_state);

        const int n_f = m_constraints[j]->getConstraintCount();
        for (int k = 0; k < n_f; ++k, ++j_f) {
            for (int i = 0; i < m_constraints[j]->m_bodyCount; ++i) {
                const int index = m_constraints[j]->m_bodies[i]->index;

                if (index == -1) continue;

                m_iv.J_sparse.setBlock(j_f, i, index);
                m_iv.J_dot_sparse.setBlock(j_f, i, index);
            }

            for (int i = 0; i < m_constraints[j]->m_bodyCount * 3; ++i) {
                const int index = m_constraints[j]->m_bodies[i / 3]->index;

                if (index == -1) continue;

                m_iv.J_sparse.set(j_f, i / 3, i % 3,
                        constraintOutput.J[k][i]);

                m_iv.J_dot_sparse.set(j_f, i / 3, i % 3,
                        constraintOutput.J_dot[k][i]);

                m_iv.ks.set(0, j_f, constraintOutput.ks[k]);
                m_iv.kd.set(0, j_f, constraintOutput.kd[k]);
                m_iv.C.set(0, j_f, constraintOutput.C[k]);
            }
        }
    }

    m_iv.J_sparse.multiply(m_iv.q_dot, &m_iv.reg0);
    for (int i = 0; i < m_f; ++i) {
        m_iv.kd.set(0, i, m_iv.kd.get(0, i) * m_iv.reg0.get(0, i));
        m_iv.ks.set(0, i, m_iv.ks.get(0, i) * m_iv.C.get(0, i));
    }

    m_iv.F_ext.initialize(1, 3 * n, 0.0);
    for (int i = 0; i < n; ++i) {
        m_iv.F_ext.set(0, i * 3 + 0, m_state.f_x[i]);
        m_iv.F_ext.set(0, i * 3 + 1, m_state.f_y[i]);
        m_iv.F_ext.set(0, i * 3 + 2, m_state.t[i]);
    }

    m_iv.F_ext.leftScale(m_iv.M_inv, &m_iv.reg2);
    m_iv.J_sparse.multiply(m_iv.reg2, &m_iv.reg0);

    m_iv.J_dot_sparse.multiply(m_iv.q_dot, &m_iv.reg2);
    m_iv.reg2.negate(&m_iv.reg1);

    m_iv.reg1.subtract(m_iv.reg0, &m_iv.reg2);
    m_iv.reg2.subtract(m_iv.ks, &m_iv.reg0);
    m_iv.reg0.subtract(m_iv.kd, &m_iv.right);

    auto s1 = std::chrono::steady_clock::now();

    const bool solvable =
        m_sleSolver->solve(
            m_iv.J_sparse,
            m_iv.M_inv,
            m_iv.right,
            &m_iv.lambda,
            &m_iv.lambda);
    assert(solvable);

    auto s2 = std::chrono::steady_clock::now();

    // Constraint force derivation
    //  R = J_T * lambda_scale
    //  => transpose(J) * transpose(transpose(lambda_scale)) = R
    //  => transpose(lambda_scale * J) = R
    //  => transpose(J.leftScale(lambda_scale)) = R

    m_iv.J_sparse.leftScale(m_iv.lambda, &m_iv.sreg0);

    for (int i = 0; i < m_f; ++i) {
        for (int j = 0; j < 2; ++j) {
            m_state.r_x[i * 2 + j] = m_iv.sreg0.get(i, j, 0);
            m_state.r_y[i * 2 + j] = m_iv.sreg0.get(i, j, 1);
            m_state.r_t[i * 2 + j] = m_iv.sreg0.get(i, j, 2);
        }
    }

    for (int i = 0; i < n; ++i) {
        m_state.a_x[i] = m_iv.F_ext.get(0, i * 3 + 0);
        m_state.a_y[i] = m_iv.F_ext.get(0, i * 3 + 1);
        m_state.a_theta[i] = m_iv.F_ext.get(0, i * 3 + 2);
    }

    for (int i = 0, j_f = 0; i < m; ++i) {
        Constraint *constraint = m_constraints[i];

        const int n_f = constraint->getConstraintCount();
        for (int j = 0; j < n_f; ++j, ++j_f) {
            for (int k = 0; k < constraint->m_bodyCount; ++k) {
                const int body = constraint->m_bodies[k]->index;
                m_state.a_x[body] += m_state.r_x[j_f * 2 + k];
                m_state.a_y[body] += m_state.r_y[j_f * 2 + k];
                m_state.a_theta[body] += m_state.r_t[j_f * 2 + k];
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        const double invMass = m_iv.M_inv.get(0, i * 3 + 0);
        const double invInertia = m_iv.M_inv.get(0, i * 3 + 2);

        m_state.a_x[i] *= invMass;
        m_state.a_y[i] *= invMass;
        m_state.a_theta[i] *= invInertia;
    }

    auto s3 = std::chrono::steady_clock::now();

    *evalTime =
        std::chrono::duration_cast<std::chrono::microseconds>(s1 - s0 + s3 - s2).count();
    *solveTime =
        std::chrono::duration_cast<std::chrono::microseconds>(s2 - s1).count();
}
