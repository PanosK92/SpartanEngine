#include "pch.h"
#include "optimized_nsv_rigid_body_system.h"

#include <chrono>
#include <cmath>

atg_scs::OptimizedNsvRigidBodySystem::OptimizedNsvRigidBodySystem() {
    m_sleSolver = nullptr;
    m_biasFactor = 1.0;
}

atg_scs::OptimizedNsvRigidBodySystem::~OptimizedNsvRigidBodySystem() {
    m_iv.J_sparse.destroy();
    m_iv.sreg0.destroy();
    m_iv.C.destroy();
    m_iv.M.destroy();
    m_iv.M_inv.destroy();
    m_iv.b_err.destroy();
    m_iv.v_bias.destroy();
    m_iv.limits.destroy();
    m_iv.q_dot.destroy();
    m_iv.q_dot_prime.destroy();
    m_iv.reg0.destroy();
    m_iv.reg1.destroy();
    m_iv.reg2.destroy();
    m_iv.right.destroy();
    m_iv.F_ext.destroy();
    m_iv.F_C.destroy();
    m_iv.R.destroy();
    m_iv.lambda.destroy();
}

void atg_scs::OptimizedNsvRigidBodySystem::initialize(SleSolver *sleSolver) {
    m_sleSolver = sleSolver;
}

void atg_scs::OptimizedNsvRigidBodySystem::process(double dt, int steps) {
    long long
        odeSolveTime = 0,
        constraintSolveTime = 0,
        forceEvalTime = 0,
        constraintEvalTime = 0;

    populateSystemState();
    populateMassMatrices(&m_iv.M, &m_iv.M_inv);

    for (int i = 0; i < steps; ++i) {
        m_odeSolver.start(&m_state, dt / steps);

        while (true) {
            const bool done = m_odeSolver.step(&m_state);

            long long evalTime = 0, solveTime = 0;

            auto s0 = std::chrono::steady_clock::now();
            processForces();
            auto s1 = std::chrono::steady_clock::now();

            processConstraints(dt / steps, &evalTime, &solveTime);

            auto s2 = std::chrono::steady_clock::now();
            m_odeSolver.solve(&m_state);
            auto s3 = std::chrono::steady_clock::now();

            constraintSolveTime += solveTime;
            constraintEvalTime += evalTime;
            odeSolveTime +=
                std::chrono::duration_cast<std::chrono::microseconds>(s3 - s2).count();
            forceEvalTime +=
                std::chrono::duration_cast<std::chrono::microseconds>(s1 - s0).count();

            if (done) break;
        }

        m_odeSolver.end();
    }

    propagateResults();

    m_odeSolveMicroseconds[m_frameIndex] = odeSolveTime;
    m_constraintSolveMicroseconds[m_frameIndex] = constraintSolveTime;
    m_forceEvalMicroseconds[m_frameIndex] = forceEvalTime;
    m_constraintEvalMicroseconds[m_frameIndex] = constraintEvalTime;
    m_frameIndex = (m_frameIndex + 1) % ProfilingSamples;
}

void atg_scs::OptimizedNsvRigidBodySystem::propagateResults() {
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
                constraint->F_x[j][k] = m_state.r_x[i_f * 2 + k];
                constraint->F_y[j][k] = m_state.r_y[i_f * 2 + k];
                constraint->F_t[j][k] = m_state.r_t[i_f * 2 + k];
            }
        }
    }
}

void atg_scs::OptimizedNsvRigidBodySystem::processConstraints(
        double dt,
        long long *evalTime,
        long long *solveTime)
{
    *evalTime = -1;
    *solveTime = -1;

    auto s0 = std::chrono::steady_clock::now();

    const int n = getRigidBodyCount();
    const int m_f = getFullConstraintCount();
    const int m = getConstraintCount();

    m_iv.J_sparse.initialize(3 * n, m_f);
    m_iv.v_bias.initialize(1, m_f);
    m_iv.C.initialize(1, m_f);
    m_iv.limits.initialize(2, m_f);

    Constraint::Output constraintOutput;
    for (int j = 0, j_f = 0; j < m; ++j) {
        m_constraints[j]->calculate(&constraintOutput, &m_state);

        const int n_f = m_constraints[j]->getConstraintCount();
        for (int k = 0; k < n_f; ++k, ++j_f) {
            for (int i = 0; i < m_constraints[j]->m_bodyCount; ++i) {
                const int index = m_constraints[j]->m_bodies[i]->index;

                if (index == -1) continue;

                m_iv.J_sparse.setBlock(j_f, i, index);
            }

            for (int i = 0; i < m_constraints[j]->m_bodyCount * 3; ++i) {
                const int index = m_constraints[j]->m_bodies[i / 3]->index;

                if (index == -1) continue;

                m_iv.J_sparse.set(j_f, i / 3, i % 3,
                        constraintOutput.J[k][i]);
            }

            m_iv.v_bias.set(0, j_f, constraintOutput.v_bias[k]);
            m_iv.C.set(0, j_f, constraintOutput.C[k]);
            m_iv.limits.set(0, j_f, constraintOutput.limits[k][0] * dt);
            m_iv.limits.set(1, j_f, constraintOutput.limits[k][1] * dt);
        }
    }

    m_iv.q_dot.resize(1, n * 3);
    for (int i = 0; i < n; ++i) {
        m_iv.q_dot.set(0, i * 3 + 0, m_state.v_x[i]);
        m_iv.q_dot.set(0, i * 3 + 1, m_state.v_y[i]);
        m_iv.q_dot.set(0, i * 3 + 2, m_state.v_theta[i]);
    }

    m_iv.F_ext.initialize(1, 3 * n, 0.0);
    for (int i = 0; i < n; ++i) {
        m_iv.F_ext.set(0, i * 3 + 0, m_state.f_x[i]);
        m_iv.F_ext.set(0, i * 3 + 1, m_state.f_y[i]);
        m_iv.F_ext.set(0, i * 3 + 2, m_state.t[i]);
    }

    // Calculate q_dot_prime
    //  q_dot_prime = q_dot + M_inv * F_ext * dt
    m_iv.F_ext.scale(dt, &m_iv.reg0);
    m_iv.reg0.leftScale(m_iv.M_inv, &m_iv.reg1);
    m_iv.reg1.add(m_iv.q_dot, &m_iv.q_dot_prime);

    // Calculate b_err
    //  b_err = (bias_factor / dt) * C
    m_iv.C.scale(m_biasFactor / dt, &m_iv.b_err);

    // Calculate right side of linear equation
    //  -(J * q_dot_prime + v_bias + b_err)
    m_iv.J_sparse.multiply(m_iv.q_dot_prime, &m_iv.reg0);
    m_iv.reg0.add(m_iv.v_bias, &m_iv.reg1);
    m_iv.reg1.add(m_iv.b_err, &m_iv.reg0);
    m_iv.reg0.negate(&m_iv.right);

    auto s1 = std::chrono::steady_clock::now();

    bool solvable = false;
    if (!m_sleSolver->supportsLimits()) {
        solvable =
            m_sleSolver->solve(
                m_iv.J_sparse,
                m_iv.M_inv,
                m_iv.right,
                &m_iv.lambda,
                &m_iv.lambda);
    }
    else {
        solvable =
            m_sleSolver->solveWithLimits(
                m_iv.J_sparse,
                m_iv.M_inv,
                m_iv.right,
                m_iv.limits,
                &m_iv.lambda,
                &m_iv.lambda);
    }

    assert(solvable);

    auto s2 = std::chrono::steady_clock::now();

    // Constraint force derivation
    //  R = J_T * lambda_scale
    //  => transpose(J) * transpose(transpose(lambda_scale)) = R
    //  => transpose(lambda_scale * J) = R
    //  => transpose(J.leftScale(lambda_scale)) = R

    m_iv.lambda.scale(1 / dt, &m_iv.reg0);
    m_iv.J_sparse.leftScale(m_iv.reg0, &m_iv.sreg0);

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
