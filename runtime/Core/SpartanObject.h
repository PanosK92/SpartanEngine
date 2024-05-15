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

//= INCLUDES ===========
#include <string>
#include "Definitions.h"
//======================

namespace Spartan
{
    // globals
    extern uint64_t g_id;

    class SP_CLASS SpartanObject
    {
    public:
        SpartanObject();
        
        // name
        const std::string& GetObjectName() const    { return m_object_name; }
        void SetObjectName(const std::string& name) { m_object_name = name; }

        // id
        const uint64_t GetObjectId() const  { return m_object_id; }
        void SetObjectId(const uint64_t id) { m_object_id = id; }
        static uint64_t GenerateObjectId();

        // sizes
        const uint64_t GetObjectSize() const { return m_object_size; }

    protected:
        std::string m_object_name;
        uint64_t m_object_id   = 0;
        uint64_t m_object_size = 0;
    };
}
