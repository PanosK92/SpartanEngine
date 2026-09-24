/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===========
#include <chrono>
#include "Definitions.h"
//======================

namespace spartan
{
    class Stopwatch
    {
    public:
        Stopwatch() { Start(); }
        ~Stopwatch() = default;

        void Start()
        {
            m_start = std::chrono::high_resolution_clock::now();
        }

        float GetElapsedTimeSec() const
        {
            const std::chrono::duration<double, std::milli> ms = std::chrono::high_resolution_clock::now() - m_start;
            return static_cast<float>(ms.count() / 1000);
        }

        float GetElapsedTimeMs() const
        {
            const std::chrono::duration<double, std::milli> ms = std::chrono::high_resolution_clock::now() - m_start;
            return static_cast<float>(ms.count());
        }

    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> m_start;
    };
}
