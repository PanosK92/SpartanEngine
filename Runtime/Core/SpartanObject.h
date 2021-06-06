/*
Copyright(c) 2016-2021 Panos Karabelas

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
    extern uint32_t g_id;

    class SPARTAN_CLASS SpartanObject
    {
    public:
        SpartanObject(Context* context = nullptr);

        // Name
        const std::string& GetObjectName() const { return m_object_name; }

        // Id
        const uint32_t GetObjectId()          const { return m_object_id; }
        void SetObjectId(const uint32_t id)         { m_object_id = id; }
        static uint32_t GenerateObjectId()          { return ++g_id; }

        // CPU & GPU sizes
        const uint64_t GetObjectSizeCpu()     const { return m_object_size_cpu; }
        const uint64_t GetObjectSizeGpu()     const { return m_object_size_gpu; }

        // Execution context.
        Context* GetContext()           const { return m_context; }

    protected:
        std::string m_object_name;
        uint32_t m_object_id       = 0;
        uint64_t m_object_size_cpu = 0;
        uint64_t m_object_size_gpu = 0;

        // Execution context
        Context* m_context = nullptr;
    };
}
