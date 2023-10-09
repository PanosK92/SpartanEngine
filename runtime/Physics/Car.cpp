/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES =======================================
#include "pch.h"
#include "Car.h"
#include "Physics.h"
#include "BulletPhysicsHelper.h"
#include "../Input/Input.h"
#include "../World/Components/Transform.h"
SP_WARNINGS_OFF
#include "BulletDynamics/Vehicle/btRaycastVehicle.h"
SP_WARNINGS_ON
//==================================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace tuning
    {
        constexpr float torque_max             = 8000.0f; // newtons
        constexpr float tire_friction          = 0.9999f;
        constexpr float brake_force_max        = 2000.0f; // newtons
        constexpr float brake_ramp_speed       = 100.0f;  // how quickly the brake force ramps up
        constexpr float suspension_stiffness   = 30.0f;
        constexpr float suspension_compression = 0.83f;
        constexpr float suspension_damping     = 1.0f;
        constexpr float suspension_force_max   = 6000.0f; // newtons
        constexpr float suspension_travel_max  = 500.0f;  // centimeters
    }

    namespace tire_friction_model
    {
        float compute_slip_angle(btWheelInfo* wheel_info, const btVector3 wheel_velocity)
        {
            btVector3 wheel_forward_dir = wheel_info->m_worldTransform.getBasis().getColumn(2); // assuming the z-axis is the forward direction
    
            float v_x = wheel_forward_dir.dot(wheel_velocity);
            float v_y = wheel_info->m_raycastInfo.m_wheelAxleWS.dot(wheel_velocity);
    
            return atan2(v_y, v_x);
        }
    
        float compute_slip_ratio(btWheelInfo* wheel_info, btRigidBody* chassis)
        {
            btVector3 wheel_velocity    = chassis->getVelocityInLocalPoint(wheel_info->m_chassisConnectionPointCS);
            btVector3 wheel_forward_dir = wheel_info->m_worldTransform.getBasis().getColumn(2); // assuming the z-axis is the forward direction
            float longitudinal_velocity = wheel_forward_dir.dot(wheel_velocity);
    
            float wheel_radius     = wheel_info->m_wheelsRadius;
            float angular_velocity = wheel_info->m_deltaRotation / static_cast<float>(Timer::GetDeltaTimeSec());
            return (wheel_radius * angular_velocity - longitudinal_velocity) / Math::Helper::Max<float>(fabs(longitudinal_velocity), Math::Helper::EPSILON);
        }
        
        float compute_pacejka_force(float slip_angle_rad, float normal_load)
        {
            // coefficients come from https://en.wikipedia.org/wiki/Hans_B._Pacejka
            float B  = 0.714f; // stiffness - controls the stiffness of the tire, affecting how quickly the tire reaches its maximum force as slip increases
            float C  = 1.4f;   // shape     - affects the shape of the curve, controlling how rounded the curve is at the peak
            float D0 = 1.0f;   // peak      - sets the maximum value of the curve, representing the maximum force the tire can generate
            float E  = -0.2f;  // curvature - controls the curvature of the curve, particularly at the peak, affecting how sharply the force drops off as slip continues to increase beyond the peak
            float X  = slip_angle_rad * Math::Helper::RAD_TO_DEG;
    
            // assume a linear relationship between normal load and peak force
            float D = D0 * normal_load;
    
            // the Pacejka formula
            return D * sin(C * atan(B * X - E * (B * X - atan(B * X))));
        }
    
        void compute_tire_force(btRigidBody* chassis, btWheelInfo* wheel_info, const btVector3 wheel_velocity, btVector3* force, btVector3* force_position)
        {
            float slip_angle         = compute_slip_angle(wheel_info, wheel_velocity);
            float slip_ratio         = compute_slip_ratio(wheel_info, chassis);
            float lateral_force      = compute_pacejka_force(slip_angle, wheel_info->m_wheelsSuspensionForce);
            float longitudinal_force = compute_pacejka_force(slip_ratio, wheel_info->m_wheelsSuspensionForce);

            *force          = btVector3(longitudinal_force, 0, lateral_force);
            *force_position = wheel_info->m_worldTransform.getOrigin();
        }
    }

    void Car::Create(btRigidBody* chassis)
    {
        m_vehicle_chassis = chassis;

        // vehicle
        btRaycastVehicle::btVehicleTuning vehicle_tuning;
        {
            if (m_vehicle != nullptr)
            {
                Physics::RemoveBody(m_vehicle);
                delete m_vehicle;
                m_vehicle = nullptr;
            }

            vehicle_tuning.m_suspensionStiffness   = tuning::suspension_stiffness;
            vehicle_tuning.m_suspensionCompression = tuning::suspension_compression;
            vehicle_tuning.m_suspensionDamping     = tuning::suspension_damping;
            vehicle_tuning.m_maxSuspensionForce    = tuning::suspension_force_max;
            vehicle_tuning.m_maxSuspensionTravelCm = tuning::suspension_travel_max;
            vehicle_tuning.m_frictionSlip          = tuning::tire_friction;

            btVehicleRaycaster* vehicle_ray_caster = new btDefaultVehicleRaycaster(static_cast<btDynamicsWorld*>(Physics::GetWorld()));
            m_vehicle = new btRaycastVehicle(vehicle_tuning, m_vehicle_chassis, vehicle_ray_caster);

            m_vehicle->setCoordinateSystem(0, 1, 2); // this is needed

            Physics::AddBody(m_vehicle);
        }

        // wheels
        {
            btVector3 wheel_positions[4];

            // position of the wheels relative to the chassis
            {
                const float extent_forward  = 2.5f;
                const float extent_sideways = 1.5f;
                const float height_offset   = -0.4f;

                wheel_positions[0] = btVector3(-extent_sideways, height_offset, extent_forward - 0.05f);  // front-left
                wheel_positions[1] = btVector3(extent_sideways, height_offset, extent_forward - 0.05f);   // front-right
                wheel_positions[2] = btVector3(-extent_sideways, height_offset, -extent_forward + 0.15f); // rear-left
                wheel_positions[3] = btVector3(extent_sideways, height_offset, -extent_forward + 0.15f);  // rear-right
            }

            // add the wheels to the vehicle
            {
                btVector3 wheel_direction    = btVector3(0, -1, 0);
                btVector3 wheel_axle         = btVector3(-1, 0, 0);
                float suspension_rest_length = 0.3f;
                float wheel_radius           = 0.6f;

                for (uint32_t i = 0; i < 4; i++)
                {
                    bool is_front_wheel = i < 2;

                    m_vehicle->addWheel
                    (
                        wheel_positions[i],
                        wheel_direction,
                        wheel_axle,
                        suspension_rest_length,
                        wheel_radius,
                        vehicle_tuning,
                        is_front_wheel
                    );
                }
            }
        }
    }

    void Car::Tick()
    {
        if (!m_vehicle)
            return;

        Control();
        //ApplyTireForcesToChassis();
        UpdateTransforms();
    }

    void Car::SetWheelTransform(Transform* transform, uint32_t wheel_index)
    {
        if (wheel_index >= m_vehicle_wheel_transforms.size())
        {
            m_vehicle_wheel_transforms.resize(wheel_index + 1);
        }

        m_vehicle_wheel_transforms[wheel_index] = transform;
    }

    float Car::GetSpeedKmHour() const
    {
        return m_vehicle->getCurrentSpeedKmHour();
    }

    void Car::Control()
    {
        float delta_time_sec = static_cast<float>(Timer::GetDeltaTimeSec());
        bool handbrake       = Input::GetKey(KeyCode::Space);

        // compute torque
        if (Input::GetKey(KeyCode::Arrow_Up) || Input::GetControllerTriggerRight() != 0.0f)
        {
            m_torque_newtons   = tuning::torque_max;
            m_wants_to_reverse = false;
        }
        else if ((Input::GetKey(KeyCode::Arrow_Down) || Input::GetControllerTriggerLeft() != 0.0f))
        {
            if (GetSpeedKmHour() > 1.0f)
            {
                m_wants_to_reverse = true;
            }
            else
            {
                m_torque_newtons   = -tuning::torque_max;
                m_wants_to_reverse = false;
            }
        }
        else
        {
            m_torque_newtons   = 0.0f;
            m_wants_to_reverse = false;
        }

        // compute steering angle
        float steering_angle_target = 0.0f;
        {
            if (Input::GetKey(KeyCode::Arrow_Left) || Input::GetControllerThumbStickLeft().x < 0.0f)
            {
                steering_angle_target = -45.0f * Math::Helper::DEG_TO_RAD;
            }
            else if (Input::GetKey(KeyCode::Arrow_Right) || Input::GetControllerThumbStickLeft().x > 0.0f)
            {
                steering_angle_target = 45.0f * Math::Helper::DEG_TO_RAD;
            }

            const float return_speed = 5.0f;
            m_steering_angle_radians = Math::Helper::Lerp<float>(m_steering_angle_radians, steering_angle_target, return_speed * delta_time_sec);
        }

        // apply forces
        {
            if (m_wants_to_reverse)
            {
                // ramp up breaking force
                m_break_force = Math::Helper::Min<float>(m_break_force + tuning::brake_ramp_speed * delta_time_sec, tuning::brake_force_max);

                for (int i = 0; i < m_vehicle->getNumWheels(); ++i)
                {
                    m_vehicle->setBrake(m_break_force, i);
                }
            }
            else
            {
                // torque
                m_vehicle->applyEngineForce(m_torque_newtons, 0);
                m_vehicle->applyEngineForce(m_torque_newtons, 1);

                // ramp down breaking force
                m_break_force = Math::Helper::Max<float>(m_break_force - tuning::brake_ramp_speed * delta_time_sec, 0.0f);
                m_vehicle->setBrake(m_break_force, 0);
                m_vehicle->setBrake(m_break_force, 1);
                m_vehicle->setBrake(handbrake ? numeric_limits<float>::max() : m_break_force, 2);
                m_vehicle->setBrake(handbrake ? numeric_limits<float>::max() : m_break_force, 3);
            }

            // steering angle
            m_vehicle->setSteeringValue(m_steering_angle_radians, 0);
            m_vehicle->setSteeringValue(m_steering_angle_radians, 1);
        }
    }

    void Car::ApplyTireForcesToChassis()
    {
        btVector3 force;
        btVector3 force_position;

        for (int i = 0; i < m_vehicle->getNumWheels(); ++i)
        {
            // get wheel info
            btWheelInfo* wheel_info = &m_vehicle->getWheelInfo(i);
            btVector3 wheel_velocity = m_vehicle_chassis->getVelocityInLocalPoint(wheel_info->m_chassisConnectionPointCS);

            // compute the tire force exerted onto the body
            tire_friction_model::compute_tire_force(m_vehicle_chassis, &m_vehicle->getWheelInfo(i), wheel_velocity, &force, &force_position);

            // apply that force to the chassis at the correct position
            m_vehicle_chassis->applyForce(force, force_position);
        }
    }

    void Car::UpdateTransforms()
    {
        // steering wheel
        if (m_vehicle_steering_wheel_transform)
        {
            m_vehicle_steering_wheel_transform->SetRotationLocal(Quaternion::FromEulerAngles(0.0f, 0.0f, -m_steering_angle_radians * Math::Helper::RAD_TO_DEG));
        }

        // wheels
        for (uint32_t wheel_index = 0; wheel_index < static_cast<uint32_t>(m_vehicle_wheel_transforms.size()); wheel_index++)
        {
            if (Transform* transform = m_vehicle_wheel_transforms[wheel_index])
            {
                // update and get the wheel transform from bullet
                m_vehicle->updateWheelTransform(wheel_index, true);
                btTransform& transform_bt = m_vehicle->getWheelInfo(wheel_index).m_worldTransform;

                // set the bullet transform to the wheel transform
                transform->SetPosition(ToVector3(transform_bt.getOrigin()));

                // ToQuaternion() works with everything but the wheels, I suspect that this is because bullet uses a different
                // rotation order since it's using a right-handed coordinate system, hence a simple quaternion conversion won't work
                float x, y, z;
                transform_bt.getRotation().getEulerZYX(x, y, z);
                float steering_angle_rad = m_vehicle->getSteeringValue(wheel_index);
                Quaternion rotation = Quaternion::FromEulerAngles(z * Math::Helper::RAD_TO_DEG, steering_angle_rad * Math::Helper::RAD_TO_DEG, 0.0f);
                transform->SetRotationLocal(rotation);
            }
        }
    }
}
