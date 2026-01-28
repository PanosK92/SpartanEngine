/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ====
#include <string>
//===============

namespace spartan
{
    class SpartanObject : public RefCounted 
    {
    public:
        SpartanObject()
        {
            // stack-only, deterministic pseudo-random ID
            auto time_now = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());

            // simple stack-safe thread-unique value
            uint64_t thread_unique = reinterpret_cast<uint64_t>(GetThreadUniqueAddress());

            uint64_t random_value  = (time_now ^ thread_unique) * 2654435761u;
            random_value          ^= (random_value >> 16);

            m_object_id = random_value;
        }
        
        // name
        const std::string& GetObjectName() const    { return m_object_name; }
        void SetObjectName(const std::string& name) { m_object_name = name; }

        // id
        const uint64_t GetObjectId() const  { return m_object_id; }
        void SetObjectId(const uint64_t id) { m_object_id = id; }

        // sizes
        const uint64_t GetObjectSize() const { return m_object_size; }

    protected:
        std::string m_object_name;
        uint64_t m_object_id   = 0;
        uint64_t m_object_size = 0;

    private:
        static void* GetThreadUniqueAddress()
        {
            thread_local int dummy;
            return &dummy;
        }
    };
}
