/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include <memory>
#include "SpartanDefinitions.h"
//=============================

namespace Spartan
{
    class Context;

    enum class EngineMode : uint32_t
    {
        Physics,
        Game
    };

    class SPARTAN_CLASS Engine
    {
    public:
        Engine();
        ~Engine();

        // Performs a simulation cycle
        void Tick() const;

        //  Flags
        void SetFlag(const EngineMode flag)          { m_flags |= (1U << static_cast<uint32_t>(flag)); }
        void RemoveFlag(const EngineMode flag)       { m_flags &= ~(1U << static_cast<uint32_t>(flag)); }
        bool IsFlagSet(const EngineMode flag)  const { return m_flags & (1U << static_cast<uint32_t>(flag)); }
        void ToggleFlag(const EngineMode flag)       { IsFlagSet(flag) ? RemoveFlag(flag) : SetFlag(flag); }

        auto GetContext() const { return m_context.get(); }

    private:
        uint32_t m_flags = 0;
        std::shared_ptr<Context> m_context;
    };
}
