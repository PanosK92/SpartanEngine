/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
