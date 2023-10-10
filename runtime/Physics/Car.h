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

    class SP_CLASS Car
    {
    public:
        Car() = default;
        ~Car() = default;

        // main
        void Create(btRigidBody* chassis);
        void Tick();

        // transforms
        void SetWheelTransform(Transform* transform, uint32_t wheel_index);
        void SetSteeringWheelTransform(Transform* transform) { m_vehicle_steering_wheel_transform = transform; }

        // misc
        float GetSpeedKmHour() const;

    private:
        void Control();
        void ApplyTireForces();
        void UpdateTransforms();

        float m_torque_newtons                        = 0.0f;
        float m_steering_angle_radians                = 0.0f;
        float m_break_force                           = 0.0f;
        bool m_wants_to_reverse                       = false;
        bool m_is_at_rest                            = true;
        btRaycastVehicle* m_vehicle                   = nullptr;
        Transform* m_vehicle_steering_wheel_transform = nullptr;
        btRigidBody* m_vehicle_chassis                = nullptr;
        std::vector<Transform*> m_vehicle_wheel_transforms;
    };
}
