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
#include "../Rendering/Renderer.h"
#include "../Input/Input.h"
#include "../World/Components/Transform.h"
SP_WARNINGS_OFF
#include <BulletDynamics/Vehicle/btRaycastVehicle.h>
SP_WARNINGS_ON
//==================================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    // 1. this simulation relies on bullet physics but can be transfered elsewhere
    // 2. the definitive handling factor is the tire friction model, everything else is complementary and adds to the realism

    namespace tuning
    {
        // description:
        // the tuning parameters of the vehicle
        // these parameters control the behavior of various vehicle systems such as the engine, tires, suspension, gearbox and the anti-roll bar
        // adjusting these parameters will affect the vehicle's performance and handling characteristics

        // notes:
        // 1. units are expressed in SI units (meters, newtons, seconds etc)
        // 2. these values simulate a mid size car and need to be adjusted according to the simulated car's specifications

        // engine
        constexpr float engine_torque_max = 350;     // maximum torque output of the engine
        constexpr float engine_max_rpm    = 6500.0f; // maximum engine RPM
        constexpr float engine_idle_rpm   = 800.0f;  // idle engine RPM

        // gearbox
        constexpr float gear_ratios[]           = { 3.5f, 2.25f, 1.6f, 1.15f, 0.9f, 0.75f }; // gear ratios for each gear
        constexpr float final_drive_ratio       = 3.5f;                                      // final drive ratio
        constexpr float transmission_efficiency = 0.95f;                                     // there is some loss of torque (due to the the clutch and flywheel)
        constexpr float shift_delay             = 0.3f;                                      // gear shift delay in seconds

        // suspension
        constexpr float suspension_stiffness   = 50.0f;                    // stiffness of suspension springs in N/m
        constexpr float suspension_damping     = 2.0f;                     // damping coefficient to dissipate energy
        constexpr float suspension_compression = 1.0f;                     // compression damping coefficient
        constexpr float suspension_force_max   = 5000.0f;                  // maximum force suspension can exert in newtons
        constexpr float suspension_length      = 0.35f;                    // spring length
        constexpr float suspension_rest_length = suspension_length * 0.8f; // spring length at equilibrium
        constexpr float suspension_travel_max  = suspension_length * 0.5f; // maximum travel of the suspension
        
        // anti-roll bar
        constexpr float anti_roll_bar_stiffness_front = 500.0f; // higher front stiffness reduces oversteer, lower increases it
        constexpr float anti_roll_bar_stiffness_rear  = 500.0f; // higher rear stiffness reduces understeer, lower increases it

        // breaks
        constexpr float brake_force_max  = 1000.0f; // maximum brake force applied to wheels in newtons
        constexpr float brake_ramp_speed = 100.0f;  // rate at which brake force increases

        // steering
        constexpr float steering_angle_max    = 40.0f * Math::Helper::DEG_TO_RAD; // the maximum steering angle of the front wheels
        constexpr float steering_return_speed = 5.0f;                             // the speed at which the steering wheel returns to center
        
        // misc
        constexpr float wheel_radius          = 0.6f;  // radius of the wheel
        constexpr float tire_friction         = 2.5f;  // coefficient of friction for tires
        constexpr float aerodynamic_downforce = 0.25f; // the faster the vehicle, the more the tires will grip the road

        // wheel indices (used for bullet physics)
        constexpr uint8_t wheel_fl = 0;
        constexpr uint8_t wheel_fr = 1;
        constexpr uint8_t wheel_rl = 2;
        constexpr uint8_t wheel_rr = 3;
    }

    namespace tire_friction_model
    {
        // description:
        // the tire friction model of the vehicle is what defines most of it's handling characteristics
        // tire models are essential for simulating the interaction between the tires and the road surface
        // they compute the forces generated by tires based on various factors like slip angle, slip ratio, and normal load
        // these forces are critical for accurately simulating vehicle dynamics and handling characteristics
        // the below functions compute the slip ratios, slip angles, and ultimately the tire forces applied to the vehicle

        // notes:
        // 1. all computations are done in world space
        // 2. the y axis of certain vectors is zeroed out, this is because pacejka's formula is only concerned with forward and side slip (and to iron out any numerical imprecision)
        // 3. some vector swizzling happens, this is because the engine is using a left-handed coordinate system but bullet is using a right-handed coordinate system
        // 4. precision issues and fuzziness, in various math/vectors, can be reduced by increasing the physics simulation rate, we are doing 200hz

        btVector3 compute_wheel_direction_forward(btWheelInfo* wheel_info)
        {
            btVector3 forward_right_handed = wheel_info->m_worldTransform.getBasis().getColumn(0).normalized();
            btVector3 forward_left_handed  = btVector3(forward_right_handed.z(), forward_right_handed.y(), -forward_right_handed.x());

            return btVector3(forward_left_handed.x(), 0.0f, forward_left_handed.z());
        }

        btVector3 compute_wheel_direction_right(btWheelInfo* wheel_info)
        {
            btVector3 side = compute_wheel_direction_forward(wheel_info).cross(btVector3(0, 1, 0));
            return side.fuzzyZero() ? btVector3(1, 0, 0) : side.normalized();
        }

        btVector3 compute_wheel_velocity(btWheelInfo* wheel_info, btRigidBody* m_vehicle_chassis)
        {
            float wheel_radius         = wheel_info->m_wheelsRadius;
            btVector3 velocity_angular = m_vehicle_chassis->getAngularVelocity().cross(-wheel_info->m_raycastInfo.m_wheelAxleWS) * wheel_radius;
            btVector3 velocity_linear  = m_vehicle_chassis->getVelocityInLocalPoint(wheel_info->m_raycastInfo.m_contactPointWS);
            btVector3 velocity_total   = velocity_angular + velocity_linear;

            return btVector3(velocity_total.x(), 0.0f, velocity_total.z());
        }

        float compute_slip_ratio(btWheelInfo* wheel_info, const btVector3& wheel_forward, const btVector3& wheel_velocity, const btVector3& vehicle_velocity)
        {    
            // value meanings
            //  0:       tire is rolling perfectly without any slip
            //  0 to  1: the tire is beginning to slip under acceleration
            // -1 to  0: the tire is beginning to slip under braking
            //  1 or -1: a full throttle lock or brake lock respectively, where the tire is spinning freely (or sliding) without providing traction

            // slip ratio as defined by Springer Handbook of Robotics
            float velocity_forward = vehicle_velocity.dot(wheel_forward);
            float velocity_wheel   = wheel_velocity.dot(wheel_forward);
            float nominator        = velocity_wheel - velocity_forward;
            float denominator      = velocity_forward;

            // to avoid a division by zero, or computations with fuzzy zero values which can yield erratic slip ratios,
            // we have to slightly deviate from the formula definition (additions and clamp), but the results are still accurate enough
            return Math::Helper::Clamp<float>((nominator + Math::Helper::SMALL_FLOAT) / (denominator + Math::Helper::SMALL_FLOAT), -1.0f, 1.0f);
        }

        float compute_slip_angle(btWheelInfo* wheel_info, const btVector3& wheel_forward, const btVector3& wheel_side, const btVector3& vehicle_velocity)
        {
            // slip angle value meaning (the comments use degrees but this function returns a value from -1 to 1)
            // 0°:                     the direction of the wheel is aligned perfectly with the direction of the travel
            // 0° to 90° (-90° to 0°): the wheel is starting to turn away from the direction of travel
            // 90° (-90°):             the wheel is perpendicular to the direction of the travel, maximum lateral sliding

            btVector3 vehicle_velocity_normalized = vehicle_velocity.fuzzyZero() ? btVector3(0.0f, 0.0f, 0.0f) : vehicle_velocity.normalized();
            float vehicle_dot_wheel_forward       = vehicle_velocity_normalized.dot(wheel_forward);
            float vehicle_dot_wheel_side          = vehicle_velocity_normalized.dot(wheel_side);
            float slip_angle                      = atan2(vehicle_dot_wheel_side + Math::Helper::SMALL_FLOAT, vehicle_dot_wheel_forward + Math::Helper::SMALL_FLOAT);

            // convert radians to -1 to 1 range 
            return slip_angle / Math::Helper::PI;
        }

        float compute_pacejka_force(float slip_percentage, float normal_load)
        {
            // https://en.wikipedia.org/wiki/Hans_B._Pacejka

            // formula doesn't handle zero loads (NaN)
            if (normal_load == 0.0f)
                return 0.0f;

            // coefficients from the pacejka '94 model
            // reference: https://www.edy.es/dev/docs/pacejka-94-parameters-explained-a-comprehensive-guide/
            // b0, b2, b4, b8 are the most relevant parameters that define the curve’s shape
            float b0  = 1.5f;
            float b1  = 0.0f;
            float b2  = 1.0f;
            float b3  = 0.0f;
            float b4  = 300.0f;
            float b5  = 0.0f;
            float b6  = 0.0f;
            float b7  = 0.0f;
            float b8  = -2.0f;
            float b9  = 0.0f;
            float b10 = 0.0f;
            float b11 = 0.0f;
            float b12 = 0.0f;
            float b13 = 0.0f;

            // compute the parameters for the Pacejka ’94 formula
            float Fz  = normal_load / 1000.0f; // convert to kilonewtons
            float C   = b0;
            float D   = Fz * (b1 * Fz + b2);
            float BCD = (b3 * Fz * Fz + b4 * Fz) * exp(-b5 * Fz);
            float B   = BCD / (C * D);
            float E   = (b6 * Fz * Fz + b7 * Fz + b8) * (1 - b13 * Math::Helper::Sign(slip_percentage + (b9 * Fz + b10)));
            float H   = b9 * Fz + b10;
            float V   = b11 * Fz + b12;
            float Bx1 = B * (slip_percentage + H);

            // pacejka ’94 longitudinal formula
            return D * sin(C * atan(Bx1 - E * (Bx1 - atan(Bx1)))) + V;
        }

        void compute_tire_force(btWheelInfo* wheel_info, const btVector3& wheel_velocity, const btVector3& vehicle_velocity, btVector3* force, btVector3* force_position)
        {
            // the slip ratio and slip angle have the most influence, it's crucial
            // that their computation is accurate, otherwise the tire forces will be wrong and/or erratic

            // compute wheel directions
            btVector3 wheel_forward_dir = compute_wheel_direction_forward(wheel_info);
            btVector3 wheel_right_dir   = compute_wheel_direction_right(wheel_info);

            // a measure of how much a wheel is slipping along the direction of the vehicle travel, and it's typically concerned with the longitudinal axis of the vehicle
            float slip_ratio         = compute_slip_ratio(wheel_info, wheel_forward_dir, wheel_velocity, vehicle_velocity);
            // the angle between the direction in which a wheel is pointed and the direction in which the vehicle is actually traveling
            float slip_angle         = compute_slip_angle(wheel_info, wheel_forward_dir, wheel_right_dir, vehicle_velocity);
            // the force that the tire can exert parallel to its direction of travel
            float slip_force_forward = compute_pacejka_force(slip_ratio * 100.0f, wheel_info->m_wheelsSuspensionForce);
            // the force that the tire can exert perpendicular to its direction of travel
            float slip_force_side    = compute_pacejka_force(slip_angle * 100.0f, wheel_info->m_wheelsSuspensionForce);
            // compute the total force
            btVector3 wheel_force    = (slip_force_forward * wheel_forward_dir) + (slip_force_side * wheel_right_dir);

            //SP_LOG_INFO("slip ratio: %.4f (%.2f N), slip angle: %.4f (%.2f N)", slip_ratio, slip_force_forward, slip_angle, slip_force_side);

            // i believe that because this is the contact point between all the external physics
            // computations and bullet, there might by some differences in the simulation scale
            float simulation_scale = 50.0f; // setting the scale to something that feels correct

            *force            = btVector3(wheel_force.x(), 0.0f, wheel_force.z()) * simulation_scale;
            *force_position   = wheel_info->m_raycastInfo.m_contactPointWS;
        }
    }

    namespace anti_roll_bar
    {
        // description:
        // simulation of an anti-roll bar
        // an anti-roll bar is a crucial part in stabilizing the vehicle, especially during turns
        // it counters the roll of the vehicle on its longitudinal axis, improving the ride stability and handling
        // the function computes and applies the anti-roll force based on the difference in suspension compression between a pair of wheels

        void apply(btRaycastVehicle* vehicle, btRigidBody* chassis, int wheel_index_1, int wheel_index_2, float force)
        {
            // get the wheel suspension forces and positions
            btWheelInfo& wheel_info1 = vehicle->getWheelInfo(wheel_index_1);
            btWheelInfo& wheel_info2 = vehicle->getWheelInfo(wheel_index_2);

            // determine the anti-roll force necessary to counteract the difference in suspension compression
            float anti_roll_force = 0.0f;
            if (wheel_info1.m_raycastInfo.m_isInContact && wheel_info2.m_raycastInfo.m_isInContact)
            {
                float suspension_difference = wheel_info1.m_raycastInfo.m_suspensionLength - wheel_info2.m_raycastInfo.m_suspensionLength;
                anti_roll_force = suspension_difference * force;
            }
            else if (!wheel_info1.m_raycastInfo.m_isInContact)
            {
                anti_roll_force = -force;
            }
            else if (!wheel_info2.m_raycastInfo.m_isInContact)
            {
                anti_roll_force = force;
            }

            // apply the anti-roll forces to the wheels
            if (wheel_info1.m_raycastInfo.m_isInContact)
            {
                btVector3 anti_roll_force_vector(0, anti_roll_force, 0);
                btVector3 force_position = wheel_info1.m_raycastInfo.m_contactPointWS;
                chassis->applyForce(anti_roll_force_vector, force_position);
            }
            if (wheel_info2.m_raycastInfo.m_isInContact)
            {
                btVector3 anti_roll_force_vector(0, -anti_roll_force, 0);
                btVector3 force_position = wheel_info2.m_raycastInfo.m_contactPointWS;
                chassis->applyForce(anti_roll_force_vector, force_position);
            }
        }
    }

    namespace gearbox
    {
        // description:
        // the gearbox of the vehicle
        // it manages gear shifting and computes the torque output based on engine rpm and gear ratios
        // automatic gear shifting is implemented based on a simplistic rpm threshold logic

        void update(float& engine_rpm, uint32_t& current_gear, float& last_shift_time, bool& is_shifting, const float speed_mps)
        {
            // compute engine rpm based on vehicle speed and current gear ratio
            engine_rpm = speed_mps * (tuning::gear_ratios[current_gear - 1] * tuning::final_drive_ratio) * (1 / (tuning::wheel_radius * Math::Helper::PI * 2)) * 60;

            // automatic gear shifting logic based on rpm thresholds
            float delta_time_seconds = static_cast<float>(Timer::GetDeltaTimeSec());
            if (engine_rpm > tuning::engine_max_rpm && current_gear < (sizeof(tuning::gear_ratios) / sizeof(tuning::gear_ratios[0])) && !is_shifting)
            {
                current_gear++;
                last_shift_time = delta_time_seconds;
                is_shifting     = true;
            }
            else if (engine_rpm < tuning::engine_idle_rpm && current_gear > 1 && !is_shifting)
            {
                current_gear--;
                last_shift_time = delta_time_seconds;
                is_shifting     = true;
            }

            // reset is_shifting flag after the delay
            if (is_shifting && (delta_time_seconds - last_shift_time) > tuning::shift_delay)
            {
                is_shifting = false;
            }
        }

        float torque_curve(const float normalized_rpm)
        {
            // simplistic torque curve
            return 1.0f - 0.5f * (normalized_rpm - 0.5f) * (normalized_rpm - 0.5f);
        }

        float get_torque(const float engine_rpm, const uint32_t current_gear, const float throttle_input)
        {
            float normalized_rpm     = (engine_rpm - tuning::engine_idle_rpm) / (tuning::engine_max_rpm - tuning::engine_idle_rpm);
            float torque_curve_value = torque_curve(normalized_rpm);
            float gear_ratio         = tuning::gear_ratios[current_gear - 1] * tuning::final_drive_ratio;
            return tuning::engine_torque_max * throttle_input * gear_ratio * torque_curve_value * tuning::transmission_efficiency;
        }
    }

    namespace debug
    {
        constexpr bool draw = true;
        ostringstream oss;

        string wheel_to_string(const btRaycastVehicle* vehicle, const uint8_t wheel_index)
        {
            const btWheelInfo& wheel_info = vehicle->getWheelInfo(wheel_index);

            string wheel_name;
            switch (wheel_index)
            {
                case tuning::wheel_fl: wheel_name  = "FL";     break;
                case tuning::wheel_fr: wheel_name  = "FR";     break;
                case tuning::wheel_rl: wheel_name  = "RL";     break;
                case tuning::wheel_rr: wheel_name  = "RR";     break;
                default:               wheel_name = "Unknown"; break;
            }

            // setup ostringstream
            oss.str("");
            oss.clear();
            oss << fixed << setprecision(2);

            oss << "Wheel: "             << wheel_name << "\n";
            oss << "Steering: "          << static_cast<float>(wheel_info.m_steering) * Math::Helper::RAD_TO_DEG << " deg\n";
            oss << "Angular velocity: "  << static_cast<float>(wheel_info.m_deltaRotation) / static_cast<float>(Timer::GetDeltaTimeSec()) << " rad/s\n";
            oss << "Torque: "            << wheel_info.m_engineForce << " N\n";
            oss << "Suspension length: " << wheel_info.m_raycastInfo.m_suspensionLength << " m\n";

            return oss.str();
        }

        void draw_wheel_info(btRaycastVehicle* vehicle)
        {
            Renderer::DrawString(wheel_to_string(vehicle, tuning::wheel_fl), Vector2(0.35f, 0.005f));
            Renderer::DrawString(wheel_to_string(vehicle, tuning::wheel_fr), Vector2(0.6f,  0.005f));
            Renderer::DrawString(wheel_to_string(vehicle, tuning::wheel_rl), Vector2(0.85f,  0.005f));
            Renderer::DrawString(wheel_to_string(vehicle, tuning::wheel_rr), Vector2(1.1f,  0.005f));
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
            vehicle_tuning.m_maxSuspensionTravelCm = tuning::suspension_travel_max * 1000.0f;
            vehicle_tuning.m_frictionSlip          = tuning::tire_friction;

            btVehicleRaycaster* vehicle_ray_caster = new btDefaultVehicleRaycaster(static_cast<btDynamicsWorld*>(Physics::GetWorld()));
            m_vehicle = new btRaycastVehicle(vehicle_tuning, m_vehicle_chassis, vehicle_ray_caster);

            // this is crucial to get right
            m_vehicle->setCoordinateSystem(0, 1, 2); // X is right, Y is up, Z is forward

            Physics::AddBody(m_vehicle);
        }

        // wheels
        {
            btVector3 wheel_positions[4];

            // position of the wheels relative to the chassis
            {
                const float extent_forward  = 2.5f;
                const float extent_sideways = 1.5f;

                wheel_positions[tuning::wheel_fl] = btVector3(-extent_sideways, -tuning::suspension_length,  extent_forward + 0.05f);
                wheel_positions[tuning::wheel_fr] = btVector3( extent_sideways, -tuning::suspension_length,  extent_forward + 0.05f);
                wheel_positions[tuning::wheel_rl] = btVector3(-extent_sideways, -tuning::suspension_length, -extent_forward + 0.25f);
                wheel_positions[tuning::wheel_rr] = btVector3( extent_sideways, -tuning::suspension_length, -extent_forward + 0.25f);
            }

            // add the wheels to the vehicle
            {
                btVector3 direction_suspension = btVector3(0, -1, 0); // pointing downward along Y-axis
                btVector3 direciton_rotation   = btVector3(1, 0, 0);  // pointing along the X-axis

                for (uint32_t i = 0; i < 4; i++)
                {
                    bool is_front_wheel = i < 2;

                    m_vehicle->addWheel
                    (
                        wheel_positions[i],
                        direction_suspension,
                        direciton_rotation,
                        tuning::suspension_rest_length,
                        tuning::wheel_radius,
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

        HandleInput();
        ApplyForces();
        UpdateTransforms();

        if (debug::draw)
        {
            debug::draw_wheel_info(m_vehicle);
        }
    }

    void Car::SetWheelTransform(Transform* transform, uint32_t wheel_index)
    {
        if (wheel_index >= m_vehicle_wheel_transforms.size())
        {
            m_vehicle_wheel_transforms.resize(wheel_index + 1);
        }

        m_vehicle_wheel_transforms[wheel_index] = transform;
    }

    float Car::GetSpeedKilometersPerHour() const
    {
        return m_vehicle->getCurrentSpeedKmHour();
    }

    float Car::GetSpeedMetersPerSecond() const
    {
        return GetSpeedKilometersPerHour() * (1000.0f / 3600.0f);
    }

    void Car::HandleInput()
    {
        // compute engine torque
        m_engine_torque = 0.0f;
        {
            gearbox::update(m_engine_rpm, m_current_gear, m_last_shift_time, m_is_shifting, GetSpeedMetersPerSecond());

            float throttle_input = 0.0f;
            if (Input::GetKey(KeyCode::Arrow_Up) || Input::GetControllerTriggerRight() != 0.0f)
            {
                throttle_input = 1.0f;
            }
            else if (Input::GetKey(KeyCode::Arrow_Down) || Input::GetControllerTriggerLeft() != 0.0f)
            {
                throttle_input = -1.0f;
            }

            m_engine_torque = gearbox::get_torque(m_engine_rpm, m_current_gear, throttle_input);
        }

        // steer the front wheels
        {
            float steering_angle_target = 0.0f;

            if (Input::GetKey(KeyCode::Arrow_Left) || Input::GetControllerThumbStickLeft().x < 0.0f)
            {
                steering_angle_target = -tuning::steering_angle_max;
            }
            else if (Input::GetKey(KeyCode::Arrow_Right) || Input::GetControllerThumbStickLeft().x > 0.0f)
            {
                steering_angle_target = tuning::steering_angle_max;
            }

            // lerp to new steering angle - real life vehicles don't snap their wheels to the target angle
            m_sterring_angle = Math::Helper::Lerp<float>(m_sterring_angle, steering_angle_target, tuning::steering_return_speed * static_cast<float>(Timer::GetDeltaTimeSec()));

            // set the steering angle
            m_vehicle->setSteeringValue(m_sterring_angle, tuning::wheel_fl);
            m_vehicle->setSteeringValue(m_sterring_angle, tuning::wheel_fr);
        }
    }

    void Car::ApplyForces()
    {
        float delta_time_sec          = static_cast<float>(Timer::GetDeltaTimeSec());
        float speed_meters_per_second = GetSpeedMetersPerSecond();

        // engine torque (front-wheel drive)
        m_vehicle->applyEngineForce(-m_engine_torque, tuning::wheel_fl);
        m_vehicle->applyEngineForce(-m_engine_torque, tuning::wheel_fr);

        // aerodynamic downforce
        {
            float downforce = tuning::aerodynamic_downforce * speed_meters_per_second * speed_meters_per_second;
            btVector3 downforce_vector(0, -downforce, 0); // Y-axis is up
            m_vehicle_chassis->applyCentralForce(downforce_vector);
        }

        // anti-roll bar
        anti_roll_bar::apply(m_vehicle, m_vehicle_chassis, tuning::wheel_fl, tuning::wheel_fr, tuning::anti_roll_bar_stiffness_front);
        anti_roll_bar::apply(m_vehicle, m_vehicle_chassis, tuning::wheel_rl, tuning::wheel_rr, tuning::anti_roll_bar_stiffness_rear); 

        // tire friction model - the main factor that defines handling
        for (int i = 0; i < m_vehicle->getNumWheels(); ++i)
        {
            btWheelInfo* wheel_info = &m_vehicle->getWheelInfo(i);

            if (wheel_info->m_raycastInfo.m_isInContact)
            {
                btVector3 velocity_wheel = tire_friction_model::compute_wheel_velocity(wheel_info, m_vehicle_chassis);
                btVector3 velocity_vehicle = btVector3(m_vehicle_chassis->getLinearVelocity().x(), 0.0f, m_vehicle_chassis->getLinearVelocity().z());

                btVector3 force;
                btVector3 force_position;
                tire_friction_model::compute_tire_force(wheel_info, velocity_wheel, velocity_vehicle, &force, &force_position);

                m_vehicle_chassis->applyForce(force, force_position);
            }
        }

        // breaking
        {
            bool handbrake = Input::GetKey(KeyCode::Space);

            if (m_wants_to_reverse)
            {
                m_break_force = Math::Helper::Min<float>(m_break_force + tuning::brake_ramp_speed * delta_time_sec, tuning::brake_force_max);

                for (int i = 0; i < m_vehicle->getNumWheels(); ++i)
                {
                    m_vehicle->setBrake(m_break_force, i);
                }
            }
            else
            {
                m_break_force = Math::Helper::Max<float>(m_break_force - tuning::brake_ramp_speed * delta_time_sec, 0.0f);
                m_vehicle->setBrake(m_break_force, tuning::wheel_fl);
                m_vehicle->setBrake(m_break_force, tuning::wheel_fr);

                m_vehicle->setBrake(handbrake ? numeric_limits<float>::max() : m_break_force, tuning::wheel_rl);
                m_vehicle->setBrake(handbrake ? numeric_limits<float>::max() : m_break_force, tuning::wheel_rr);
            }
        }
    }

    void Car::UpdateTransforms()
    {
        // steering wheel
        if (m_vehicle_steering_wheel_transform)
        {
            m_vehicle_steering_wheel_transform->SetRotationLocal(Quaternion::FromEulerAngles(0.0f, 0.0f, -m_sterring_angle * Math::Helper::RAD_TO_DEG));
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
