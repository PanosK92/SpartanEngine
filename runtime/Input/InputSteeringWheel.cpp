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

//= INCLUDES ========
#include "pch.h"
#include "Input.h"
SP_WARNINGS_OFF
#include <SDL3/SDL.h>
SP_WARNINGS_ON
//===================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        Controller steering_wheel;
        float steering    = 0.0f;
        float accelerator = 0.0f;
        float brake       = 0.0f;

        float get_normalized_axis_value(const Controller& controller, uint32_t axis)
        {
            // initialize result
            float normalized = 0.0f;
        
            // check if controller is a steering wheel
            if (controller.type == ControllerType::SteeringWheel && controller.sdl_pointer)
            {
                // get raw axis value
                Sint16 raw_value = SDL_GetJoystickAxis(static_cast<SDL_Joystick*>(controller.sdl_pointer), axis);
        
                // normalize based on axis type
                if (axis == 0) // steering (axis 0)
                {
                    // normalize to [-1.0, 1.0], no deadzone
                    normalized = static_cast<float>(raw_value) / 32768.0f;
                }
                else // pedals (axis 2 for gas, 3 for brake)
                {
                    // pedals go from 32767 (unpressed) to -32768 (pressed)
                    // normalize to [0.0, 1.0]
                    normalized = static_cast<float>(32767 - raw_value) / 65535.0f;
                }
            }
        
            return normalized;
}
    }

    void Input::PollSteeringWheel()
    {
        // check if controller is connected and is a steering wheel
        if (!steering_wheel.sdl_pointer || !steering_wheel.is_connected || steering_wheel.type != ControllerType::SteeringWheel)
        {
            steering    = 0.0f;
            accelerator = 0.0f;
            brake       = 0.0f;
            return;
        }
    
        // read joystick axes for my g29 steering wheel
        steering    = get_normalized_axis_value(steering_wheel, 0); // axis 0: steering
        accelerator = get_normalized_axis_value(steering_wheel, 2); // axis 2: gas pedal
        brake       = get_normalized_axis_value(steering_wheel, 3); // axis 3: brake pedal
    
        //SP_LOG_INFO("steering: %f, accelerator: %f, brake: %f", steering, accelerator, brake);
    }

    void Input::OnEventSteeringWheel(void* event)
    {
        steering_wheel.type = ControllerType::SteeringWheel;
        CheckDeviceState(event, &steering_wheel);
    }

    float Input::GetSteeringWheelSteering()
    {
        return steering;
    }

    float Input::GetSteeringWheelAccelerator()
    {
        return accelerator;
    }

    float Input::GetSteeringWheelBrake()
    {
        return brake;
    }
}
