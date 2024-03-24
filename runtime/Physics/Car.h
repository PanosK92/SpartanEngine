/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= FORWARD DECLARATIONS =
class btRaycastVehicle;
class btRigidBody;
//========================

namespace Spartan
{
    //= FORWARD DECLARATIONS =
    class Transform;
    //========================

    enum class CarMovementState : uint8_t
    {
        Forward,
        Backward,
        Stationary
    };

    struct CarParameters
    {
        // engine
        float engine_torque                 = 0.0f;
        float engine_rpm                    = 0.0f;

        // aerodynamics
        float aerodynamics_downforce        = 0.0f;
        float aerodynamics_drag             = 0.0f;

        // gearbox
        int32_t gear                        = 0;
        bool is_shifting                    = false;
        float last_shift_time               = 0.0f;
        float gear_ratio                    = 0.0f;

        // brakes
        float break_force                   = 0.0f;
        bool break_until_opposite_torque    = false;

        // wheels
        std::array<float, 4> pacejka_slip_angle = { 0.0f, 0.0f, 0.0f, 0.0f };
        std::array<float, 4> pacejka_slip_ratio = { 0.0f, 0.0f, 0.0f, 0.0f };
        std::array<float, 4> pacejka_fz         = { 0.0f, 0.0f, 0.0f, 0.0f };
        std::array<float, 4> pacejka_fx         = { 0.0f, 0.0f, 0.0f, 0.0f };

        // misc
        float steering_angle                = 0.0f;
        float throttle                      = 0.0f;
        CarMovementState movement_direction = CarMovementState::Stationary;
        btRaycastVehicle* vehicle           = nullptr;
        btRigidBody* body                   = nullptr;
        Entity* transform_steering_wheel    = nullptr;
        std::vector<Entity*> transform_wheels;
    };

    class SP_CLASS Car
    {
    public:
        Car() = default;
        ~Car() = default;

        // main
        void Create(btRigidBody* chassis, Entity* entity);
        void Tick();

        // transforms
        void SetWheelTransform(Entity* transform, uint32_t wheel_index);
        void SetSteeringWheelTransform(Entity* transform) { m_parameters.transform_steering_wheel = transform; }

        // speed
        float GetSpeedKilometersPerHour() const;
        float GetSpeedMetersPerSecond() const;

    private:
        void HandleInput();
        void ApplyForces();
        void UpdateTransforms();

        CarParameters m_parameters;
    };
}
