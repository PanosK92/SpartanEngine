/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ====
#include <string>
#include <atomic>
//===============

namespace spartan
{
    // gives a type a unique id and a name, the base of components, resources and rhi objects
    class SpartanObject
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
        void SetObjectName(const std::string& name) { if (m_object_name != name) { m_object_name = name; ++s_identity_revision; } }
        static uint64_t GetIdentityRevision() { return s_identity_revision.load(std::memory_order_relaxed); }

        // id
        const uint64_t GetObjectId() const  { return m_object_id; }
        void SetObjectId(const uint64_t id) { if (m_object_id != id) { m_object_id = id; ++s_identity_revision; } }

        // sizes
        const uint64_t GetObjectSize() const { return m_object_size; }

    protected:
        std::string m_object_name;
        uint64_t m_object_id   = 0;
        uint64_t m_object_size = 0;

    private:
        inline static std::atomic<uint64_t> s_identity_revision{0};
        static void* GetThreadUniqueAddress()
        {
            thread_local int dummy;
            return &dummy;
        }
    };
}
