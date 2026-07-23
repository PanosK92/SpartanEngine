#include "rigid_body_system.h"

#include <assert.h>
#include <chrono>
#include <cmath>

atg_scs::RigidBodySystem::RigidBodySystem() {
    m_odeSolveMicroseconds = new long long[ProfilingSamples];
    m_constraintSolveMicroseconds = new long long[ProfilingSamples];
    m_forceEvalMicroseconds = new long long[ProfilingSamples];
    m_constraintEvalMicroseconds = new long long[ProfilingSamples];
    m_frameIndex = 0;

    for (int i = 0; i < ProfilingSamples; ++i) {
        m_odeSolveMicroseconds[i] = -1;
        m_constraintSolveMicroseconds[i] = -1;
        m_forceEvalMicroseconds[i] = -1;
        m_constraintEvalMicroseconds[i] = -1;
    }
}

atg_scs::RigidBodySystem::~RigidBodySystem() {
    delete[] m_odeSolveMicroseconds;
    delete[] m_constraintSolveMicroseconds;
    delete[] m_forceEvalMicroseconds;
    delete[] m_constraintEvalMicroseconds;

    m_state.destroy();
}

void atg_scs::RigidBodySystem::reset() {
    m_rigidBodies.clear();
    m_constraints.clear();
    m_forceGenerators.clear();
}

void atg_scs::RigidBodySystem::process(double dt, int steps) {
    /* void */
}

void atg_scs::RigidBodySystem::addRigidBody(RigidBody *body) {
    m_rigidBodies.push_back(body);
    body->index = (int)m_rigidBodies.size() - 1;
}

void atg_scs::RigidBodySystem::removeRigidBody(RigidBody *body) {
    m_rigidBodies[body->index] = m_rigidBodies.back();
    m_rigidBodies[body->index]->index = body->index;
    m_rigidBodies.resize(m_rigidBodies.size() - 1);
}

atg_scs::RigidBody *atg_scs::RigidBodySystem::getRigidBody(int i) {
    assert(i < m_rigidBodies.size());
    return m_rigidBodies[i];
}

void atg_scs::RigidBodySystem::addConstraint(Constraint *constraint) {
    m_constraints.push_back(constraint);
    constraint->m_index = (int)m_constraints.size() - 1;
}

void atg_scs::RigidBodySystem::removeConstraint(Constraint *constraint) {
    m_constraints[constraint->m_index] = m_constraints.back();
    m_constraints[constraint->m_index]->m_index = constraint->m_index;
    m_constraints.resize(m_constraints.size() - 1);
}

void atg_scs::RigidBodySystem::addForceGenerator(ForceGenerator *forceGenerator) {
    m_forceGenerators.push_back(forceGenerator);
    forceGenerator->m_index = (int)m_forceGenerators.size() - 1;
}

void atg_scs::RigidBodySystem::removeForceGenerator(ForceGenerator *forceGenerator) {
    m_forceGenerators[forceGenerator->m_index] = m_forceGenerators.back();
    m_forceGenerators[forceGenerator->m_index]->m_index = forceGenerator->m_index;
    m_forceGenerators.resize(m_forceGenerators.size() - 1);
}

int atg_scs::RigidBodySystem::getFullConstraintCount() const {
    int count = 0;
    for (Constraint *constraint: m_constraints) {
        count += constraint->getConstraintCount();
    }

    return count;
}

float atg_scs::RigidBodySystem::findAverage(long long *samples) {
    long long accum = 0;
    int count = 0;
    for (int i = 0; i < ProfilingSamples; ++i) {
        if (samples[i] != -1) {
            accum += samples[i];
            ++count;
        }
    }

    if (count == 0) return 0;
    else return (float)accum / count;
}

float atg_scs::RigidBodySystem::getOdeSolveMicroseconds() const {
    return findAverage(m_odeSolveMicroseconds);
}

float atg_scs::RigidBodySystem::getConstraintSolveMicroseconds() const {
    return findAverage(m_constraintSolveMicroseconds);
}

float atg_scs::RigidBodySystem::getConstraintEvalMicroseconds() const {
    return findAverage(m_constraintEvalMicroseconds);
}

float atg_scs::RigidBodySystem::getForceEvalMicroseconds() const {
    return findAverage(m_forceEvalMicroseconds);
}

void atg_scs::RigidBodySystem::populateSystemState() {
    const int n = getRigidBodyCount();
    const int n_c = getFullConstraintCount();
    const int m = getConstraintCount();

    m_state.resize(n, n_c);

    for (int i = 0; i < n; ++i) {
        m_state.a_x[i] = 0;
        m_state.a_y[i] = 0;

        m_state.v_x[i] = m_rigidBodies[i]->v_x;
        m_state.v_y[i] = m_rigidBodies[i]->v_y;

        m_state.p_x[i] = m_rigidBodies[i]->p_x;
        m_state.p_y[i] = m_rigidBodies[i]->p_y;

        m_state.a_theta[i] = 0;
        m_state.v_theta[i] = m_rigidBodies[i]->v_theta;
        m_state.theta[i] = m_rigidBodies[i]->theta;

        m_state.m[i] = m_rigidBodies[i]->m;
    }

    for (int i = 0, j_f = 0; i < m; ++i) {
        m_state.indexMap[i] = j_f;
        j_f += m_constraints[i]->getConstraintCount();
    }
}

void atg_scs::RigidBodySystem::populateMassMatrices(Matrix *M, Matrix *M_inv) {
    const int n = getRigidBodyCount();

    M->initialize(1, 3 * n);
    M_inv->initialize(1, 3 * n);

    for (int i = 0; i < n; ++i) {
        M->set(0, i * 3 + 0, m_rigidBodies[i]->m);
        M->set(0, i * 3 + 1, m_rigidBodies[i]->m);
        M->set(0, i * 3 + 2, m_rigidBodies[i]->I);

        M_inv->set(0, i * 3 + 0, 1 / m_rigidBodies[i]->m);
        M_inv->set(0, i * 3 + 1, 1 / m_rigidBodies[i]->m);
        M_inv->set(0, i * 3 + 2, 1 / m_rigidBodies[i]->I);
     }
}

void atg_scs::RigidBodySystem::processForces() {
    const int n_f = getForceGeneratorCount();
    const int n = getRigidBodyCount();

    for (int i = 0; i < n; ++i) {
        m_state.f_x[i] = 0.0;
        m_state.f_y[i] = 0.0;
        m_state.t[i] = 0.0;
    }

    for (int i = 0; i < n_f; ++i) {
        m_forceGenerators[i]->apply(&m_state);
    }
}
