#ifndef ATG_SIMPLE_2D_CONSTRAINT_SOLVER_RIGID_BODY_H
#define ATG_SIMPLE_2D_CONSTRAINT_SOLVER_RIGID_BODY_H

namespace atg_scs {
    struct RigidBody {
        public:
            RigidBody();
            ~RigidBody();

            void localToWorld(double x, double y, double *w_x, double *w_y);
            void worldToLocal(double x, double y, double *l_x, double *l_y);

            double p_x;
            double p_y;

            double v_x;
            double v_y;

            double theta;
            double v_theta;

            double m;
            double I;

            int index;

            void reset();
            double energy() const;
    };
} /* namespace atg_scs */

#endif /* ATG_SIMPLE_2D_CONSTRAINT_SOLVER_RIGID_BODY_H */
