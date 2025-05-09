/*
Copyright(c) 2016-2025 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

//= INCLUDES ==================
#include <btBulletDynamicsCommon.h>
#include <BulletDynamics/ConstraintSolver/btGeneric6DofSpringConstraint.h>
#include "../Input/Input.h" // adjust path if needed
#include <cmath>
//=============================

namespace spartan
{
    class Car2 {
    public:
        Car2(btDiscreteDynamicsWorld* world, btRigidBody* chassis) 
            : world_(world), chassis_(chassis) {
            // assume chassis is already added by physicsbody
    
            btCylinderShapeX* wheel_shape = new btCylinderShapeX(btVector3(0.15f, 0.5f, 0.5f));
            wheel_positions_[0] = btVector3(1.0f, -0.5f, 2.0f);  // front right
            wheel_positions_[1] = btVector3(-1.0f, -0.5f, 2.0f); // front left
            wheel_positions_[2] = btVector3(1.0f, -0.5f, -2.0f); // rear right
            wheel_positions_[3] = btVector3(-1.0f, -0.5f, -2.0f); // rear left
    
            for (int i = 0; i < 4; ++i) {
                btVector3 wheel_pos = chassis_->getCenterOfMassPosition() + wheel_positions_[i];
                btVector3 wheel_inertia;
                wheel_shape->calculateLocalInertia(10.0f, wheel_inertia);
                btDefaultMotionState* wheel_motion_state = new btDefaultMotionState(
                    btTransform(btQuaternion(0, 0, 0, 1), wheel_pos)
                );
                wheels_[i] = new btRigidBody(10.0f, wheel_motion_state, wheel_shape, wheel_inertia);
                wheels_[i]->setFriction(0.0f);
                world_->addRigidBody(wheels_[i]);
    
                btTransform frame_in_chassis(btQuaternion::getIdentity(), wheel_positions_[i]);
                btTransform frame_in_wheel(btQuaternion::getIdentity(), btVector3(0, 0, 0));
                btGeneric6DofSpringConstraint* constraint = new btGeneric6DofSpringConstraint(
                    *chassis_, *wheels_[i], frame_in_chassis, frame_in_wheel, true
                );
                constraint->setLinearLowerLimit(btVector3(0, -0.2f, 0));
                constraint->setLinearUpperLimit(btVector3(0, 0.2f, 0));
                constraint->setAngularLowerLimit(btVector3(-BT_LARGE_FLOAT, 0, 0));
                constraint->setAngularUpperLimit(btVector3(BT_LARGE_FLOAT, 0, 0));
                constraint->enableSpring(1, true);
                constraint->setStiffness(1, 10000.0f);
                constraint->setDamping(1, 1000.0f);
                constraint->setEquilibriumPoint(1, 0.0f);
                world_->addConstraint(constraint, true);
                suspension_constraints_[i] = constraint;
            }
        }
    
        ~Car2() {
            for (int i = 0; i < 4; ++i) {
                world_->removeConstraint(suspension_constraints_[i]);
                delete suspension_constraints_[i];
                world_->removeRigidBody(wheels_[i]);
                delete wheels_[i]->getMotionState();
                delete wheels_[i];
            }
        }
    
        void step_simulation(btScalar time_step) {
            float chassis_mass = chassis_->getInvMass() > 0.0f ? 1.0f / chassis_->getInvMass() : 1000.0f;
    
            // steering control
            float steer_angle = 0.0f;
            if (spartan::Input::GetKey(spartan::KeyCode::Arrow_Left)) {
                steer_angle = max_steer_angle_; // turn left
            } else if (spartan::Input::GetKey(spartan::KeyCode::Arrow_Right)) {
                steer_angle = -max_steer_angle_; // turn right
            }
    
            // apply steering to front wheels (indices 0 and 1)
            for (int i = 0; i < 2; ++i) {
                btTransform transform = wheels_[i]->getWorldTransform();
                btQuaternion current_rotation = transform.getRotation();
                btQuaternion steer_rotation(btVector3(0, 1, 0), steer_angle); // rotate around y-axis
                transform.setRotation(steer_rotation * current_rotation);
                wheels_[i]->setWorldTransform(transform);
                // reset angular velocity to avoid drifting
                wheels_[i]->setAngularVelocity(btVector3(0, 0, 0));
            }
    
            // acceleration/braking control
            float torque = 0.0f;
            if (spartan::Input::GetKey(spartan::KeyCode::Arrow_Up)) {
                torque = 1000.0f; // forward torque
            } else if (spartan::Input::GetKey(spartan::KeyCode::Arrow_Down)) {
                torque = -500.0f; // reverse/brake torque
            }
    
            // apply torque to all wheels
            for (int i = 0; i < 4; ++i) {
                if (torque != 0.0f) {
                    wheels_[i]->applyTorque(btVector3(torque, 0, 0)); // torque around x-axis
                }
    
                // pacejka tire forces
                btVector3 wheel_vel = wheels_[i]->getLinearVelocity();
                btVector3 chassis_vel = chassis_->getVelocityInLocalPoint(wheel_positions_[i]);
    
                btVector3 angular_vel = wheels_[i]->getAngularVelocity();
                float wheel_radius = 0.5f;
                float wheel_surface_speed = angular_vel.x() * wheel_radius;
    
                float ground_speed = chassis_vel.z();
                float slip_ratio = (wheel_surface_speed - ground_speed) / std::max(std::abs(ground_speed), 0.1f);
    
                float normal_load = chassis_mass * 9.81f / 4.0f;
    
                float bx = tire_params_.b;
                float cx = tire_params_.c;
                float dx = tire_params_.d * normal_load;
                float ex = tire_params_.e;
                float phi_x = (1 - ex) * slip_ratio + (ex / bx) * std::atan(bx * slip_ratio);
                float longitudinal_force = dx * std::sin(cx * std::atan(bx * phi_x));
    
                btVector3 force(0, 0, longitudinal_force);
                wheels_[i]->applyCentralForce(force);
            }
        }
    
    private:
        btDiscreteDynamicsWorld* world_;
        btRigidBody* chassis_;
        btRigidBody* wheels_[4];
        btTypedConstraint* suspension_constraints_[4];
        btVector3 wheel_positions_[4];
    
        // tire parameters for pacejka
        struct tire_params {
            float b = 10.0f; // stiffness
            float c = 1.9f;  // shape
            float d = 1.0f;  // peak (scaled by normal load)
            float e = 0.97f; // curvature
        } tire_params_;
    
        // control parameters
        static constexpr float max_steer_angle_ = 0.5f; // radians, ~28 degrees
    };

}
