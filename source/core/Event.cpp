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

//= INCLUDES =====
#include "pch.h"
#include "Event.h"
#include <mutex>
//================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        static array<map<subscription_handle, subscriber>, static_cast<uint32_t>(EventType::Max)> event_subscribers;
        static subscription_handle next_subscription_id = 1;
        // guards subscribe/unsubscribe/fire so components created on worker threads can subscribe safely
        static mutex event_mutex;
    }

    void Event::Shutdown()
    {
        lock_guard<mutex> lock(event_mutex);

        for (map<subscription_handle, subscriber>& subscribers : event_subscribers)
        {
            subscribers.clear();
        }
        next_subscription_id = 1;
    }

    subscription_handle Event::Subscribe(const EventType event_type, subscriber&& function)
    {
        lock_guard<mutex> lock(event_mutex);

        subscription_handle handle = next_subscription_id++;
        event_subscribers[static_cast<uint32_t>(event_type)][handle] = std::forward<subscriber>(function);
        return handle;
    }

    void Event::Unsubscribe(const EventType event_type, subscription_handle handle)
    {
        lock_guard<mutex> lock(event_mutex);

        auto& subscribers = event_subscribers[static_cast<uint32_t>(event_type)];
        subscribers.erase(handle);
    }

    void Event::Fire(const EventType event_type, sp_variant data /*= 0*/)
    {
        // copy under lock so handlers can subscribe/unsubscribe during fire without deadlocking
        map<subscription_handle, subscriber> subscribers_copy;
        {
            lock_guard<mutex> lock(event_mutex);
            subscribers_copy = event_subscribers[static_cast<uint32_t>(event_type)];
        }

        for (const auto& [handle, subscriber_func] : subscribers_copy)
        {
            subscriber_func(data);
        }
    }
}
