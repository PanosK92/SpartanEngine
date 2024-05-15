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

//= INCLUDES =============
#include "pch.h"
#include "SpartanObject.h"
//========================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    uint64_t g_id = 0;

    SpartanObject::SpartanObject()
    {
        m_object_id = GenerateObjectId();
    }

    uint64_t SpartanObject::GenerateObjectId()
    {
        static mt19937_64 eng{ random_device{}() };

        auto time_now     = chrono::high_resolution_clock::now().time_since_epoch().count();
        auto thread_id    = hash<thread::id>()(this_thread::get_id());
        auto random_value = eng();

        uint64_t a = static_cast<uint64_t>(time_now);
        uint64_t b = static_cast<uint64_t>(thread_id);
        uint64_t c = random_value;

        // mix function based on Knuth's multiplicative method
        // https://gist.github.com/badboy/6267743
        a *= 2654435761u;
        b *= 2654435761u;
        c *= 2654435761u;

        a ^= (a >> 16);
        b ^= (b >> 16);
        c ^= (c >> 16);

        return a + b + c;
    }
}
