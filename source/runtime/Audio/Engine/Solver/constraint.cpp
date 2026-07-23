#include "constraint.h"

#include <assert.h>
#include <string.h>

atg_scs::Constraint::Constraint(int constraintCount, int bodyCount) {
    assert(constraintCount <= MaxConstraintCount);
    assert(bodyCount <= MaxBodyCount);

    m_constraintCount = constraintCount;
    m_bodyCount = bodyCount;

    m_index = -1;

    memset(m_bodies, 0, sizeof(int) * MaxBodyCount);

    for (int i = 0; i < MaxConstraintCount; ++i) {
        for (int j = 0; j < MaxBodyCount; ++j) {
            F_x[i][j] = F_y[i][j] = F_t[i][j] = 0;
        }
    }
}

atg_scs::Constraint::~Constraint() {
    /* void */
}

void atg_scs::Constraint::calculate(Output *output, SystemState *state) {
    /* void */
}
