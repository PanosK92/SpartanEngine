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

//= INCLUDES =======
#include "pch.h"
#include "Input.h"
#include <SDL/SDL.h>
//==================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        Controller steering_wheel;
        float steering    = 0.0f;
        float accelerator = 0.0f;
        float brake       = 0.0f;
    }

    void Input::PollSteeringWheel()
    {
        if (!steering_wheel.sdl_pointer)
            return;

        steering    = GetNormalizedAxisValue(steering_wheel.sdl_pointer, SDL_CONTROLLER_AXIS_LEFTX);
        accelerator = GetNormalizedAxisValue(steering_wheel.sdl_pointer, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        brake       = GetNormalizedAxisValue(steering_wheel.sdl_pointer, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);

        SP_LOG_INFO("Steering: %f, Accelerator: %f, Brake: %f", steering, accelerator, brake);
    }

    void Input::OnEventSteeringWheel(void* event)
    {
        SDL_Event* sdl_event = static_cast<SDL_Event*>(event);
        uint32_t event_type  = sdl_event->type;
        CheckControllerState(event_type, &steering_wheel, ControllerType::SteeringWheel);
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
