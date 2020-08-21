/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "../Core/ISubsystem.h"
//=============================

//= FORWARD DECLARATIONS =
namespace FMOD
{
    class System;
}
//========================

namespace Spartan
{
    class Transform;
    class Profiler;

    class Audio : public ISubsystem
    {
    public:
        Audio(Context* context);
        ~Audio();

        //= ISubsystem ======================
        bool Initialize() override;
        void Tick(float delta_time) override;
        //===================================

        auto GetSystemFMOD() const { return m_system_fmod; }
        void SetListenerTransform(Transform* transform);

    private:
        void LogErrorFmod(int error) const;

        uint32_t m_result_fmod        = 0;
        uint32_t m_max_channels        = 32;
        float m_distance_entity        = 1.0f;
        bool m_initialized            = false;
        Transform* m_listener        = nullptr;
        Profiler* m_profiler        = nullptr;
        FMOD::System* m_system_fmod = nullptr;
    };
}
