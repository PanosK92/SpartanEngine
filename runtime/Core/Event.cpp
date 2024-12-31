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

//= INCLUDES ======
#include "pch.h"
#include "Event.h"
//=================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    namespace
    {
        static array<vector<subscriber>, static_cast<uint32_t>(EventType::Max)> event_subscribers;
    }

    void Event::Shutdown()
    {
        for (vector<subscriber>& subscribers : event_subscribers)
        {
            subscribers.clear();
        }
    }

    void Event::Subscribe(const EventType event_type, subscriber&& function)
    {
        event_subscribers[static_cast<uint32_t>(event_type)].push_back(std::forward<subscriber>(function));
    }

    void Event::Fire(const EventType event_type, sp_variant data /*= 0*/)
    {
        for (const auto& subscriber : event_subscribers[static_cast<uint32_t>(event_type)])
        {
            subscriber(data);
        }
    }
}
