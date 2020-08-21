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

//= INCLUDES ===================
#include <string>
#include "Spartan_Definitions.h"
//==============================

namespace Spartan
{
    //= FORWARD DECLARATIONS =
    class Context;
    //========================

    // Globals
    static uint32_t g_id = 0;

    class SPARTAN_CLASS Spartan_Object
    {
    public:
        Spartan_Object(Context* context = nullptr)
        {
            m_context   = context;
            m_id        = GenerateId();
        }

        // Name
        const std::string& GetName()    const { return m_name; }

        // Id
        const uint32_t GetId()          const { return m_id; }
        void SetId(const uint32_t id)          { m_id = id; }
        static uint32_t GenerateId()          { return ++g_id; }

        // CPU & GPU sizes
        const uint64_t GetSizeCpu()     const { return m_size_cpu; }
        const uint64_t GetSizeGpu()     const { return m_size_gpu; }

        // Execution context.
        Context* GetContext()           const { return m_context; }

    protected:
        // Execution context
        Context* m_context = nullptr;

        std::string m_name;
        uint32_t m_id        = 0;
        uint64_t m_size_cpu = 0;
        uint64_t m_size_gpu = 0;
    };
}
