/*
Copyright(c) 2015-2026 Panos Karabelas & Thomas Ray

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

//= INCLUDES =====================
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <typeinfo>
//================================

// Only enable tracking in debug/profile builds
#if defined(SPARTAN_DEBUG) && defined(SPARTAN_PROFILE)
    #define SPARTAN_REFCOUNT_TRACKING_ENABLED
#endif

#ifdef SPARTAN_REFCOUNT_TRACKING_ENABLED
    #include <tracy/Tracy.hpp>
#endif

namespace spartan
{

#ifdef SPARTAN_REFCOUNT_TRACKING_ENABLED

    /**
     * @brief Statistics for a specific reference-counted object type
     */
    struct RefCountStats
    {
        std::atomic<uint64_t> totalCreated{0};      // Total objects created
        std::atomic<uint64_t> totalDestroyed{0};    // Total objects destroyed
        std::atomic<uint64_t> currentAlive{0};      // Currently alive objects
        std::atomic<uint64_t> peakAlive{0};         // Peak simultaneous objects
        std::atomic<uint64_t> totalIncrements{0};   // Total ref increments
        std::atomic<uint64_t> totalDecrements{0};   // Total ref decrements
        std::atomic<uint64_t> peakRefCount{0};      // Highest ref count seen
    };

    /**
     * @brief Central tracker for all reference-counted objects
     * 
     * This singleton class tracks the lifetime and reference counts of all
     * RefCounted objects in the system. It provides Tracy integration for
     * visual profiling and debugging.
     */
    class RefCountTracker
    {
    public:
        static RefCountTracker& Get()
        {
            static RefCountTracker instance;
            return instance;
        }

        // Track object creation
        void OnObjectCreated(const void* ptr, const char* typeName, uint32_t initialRefCount = 0)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            
            auto& stats = m_Stats[typeName];
            stats.totalCreated.fetch_add(1, std::memory_order_relaxed);
            stats.currentAlive.fetch_add(1, std::memory_order_relaxed);
            
            uint64_t alive = stats.currentAlive.load(std::memory_order_relaxed);
            uint64_t peak = stats.peakAlive.load(std::memory_order_relaxed);
            while (alive > peak && !stats.peakAlive.compare_exchange_weak(peak, alive)) {}

            // Track individual object
            ObjectInfo info;
            info.typeName = typeName;
            info.currentRefCount = initialRefCount;
            info.peakRefCount = initialRefCount;
            m_Objects[ptr] = info;

            #ifdef TRACY_ENABLE
                TracyPlot(("RefCount_Alive_" + std::string(typeName)).c_str(), (int64_t)alive);
                TracyMessageC(("Created: " + std::string(typeName)).c_str(), 128, 0x00FF00);
            #endif
        }

        // Track reference increment
        void OnRefIncrement(const void* ptr, uint32_t newRefCount)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            
            auto it = m_Objects.find(ptr);
            if (it == m_Objects.end()) return;

            auto& stats = m_Stats[it->second.typeName];
            stats.totalIncrements.fetch_add(1, std::memory_order_relaxed);

            it->second.currentRefCount = newRefCount;
            if (newRefCount > it->second.peakRefCount)
            {
                it->second.peakRefCount = newRefCount;
                
                uint64_t peak = stats.peakRefCount.load(std::memory_order_relaxed);
                while (newRefCount > peak && !stats.peakRefCount.compare_exchange_weak(peak, newRefCount)) {}
            }

            #ifdef TRACY_ENABLE
                TracyPlot(("RefCount_" + std::string(it->second.typeName)).c_str(), (int64_t)newRefCount);
            #endif
        }

        // Track reference decrement
        void OnRefDecrement(const void* ptr, uint32_t newRefCount)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            
            auto it = m_Objects.find(ptr);
            if (it == m_Objects.end()) return;

            auto& stats = m_Stats[it->second.typeName];
            stats.totalDecrements.fetch_add(1, std::memory_order_relaxed);

            it->second.currentRefCount = newRefCount;

            #ifdef TRACY_ENABLE
                TracyPlot(("RefCount_" + std::string(it->second.typeName)).c_str(), (int64_t)newRefCount);
            #endif
        }

        // Track object destruction
        void OnObjectDestroyed(const void* ptr)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            
            auto it = m_Objects.find(ptr);
            if (it == m_Objects.end()) return;

            auto& stats = m_Stats[it->second.typeName];
            stats.totalDestroyed.fetch_add(1, std::memory_order_relaxed);
            stats.currentAlive.fetch_sub(1, std::memory_order_relaxed);

            uint64_t alive = stats.currentAlive.load(std::memory_order_relaxed);

            #ifdef TRACY_ENABLE
                TracyPlot(("RefCount_Alive_" + std::string(it->second.typeName)).c_str(), (int64_t)alive);
                TracyMessageC(("Destroyed: " + std::string(it->second.typeName)).c_str(), 128, 0xFF0000);
            #endif

            m_Objects.erase(it);
        }

        // Get statistics for a specific type
        RefCountStats GetStats(const char* typeName) const
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto it = m_Stats.find(typeName);
            if (it != m_Stats.end())
                return it->second;
            return RefCountStats{};
        }

        // Get all tracked types
        std::vector<std::string> GetTrackedTypes() const
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            std::vector<std::string> types;
            types.reserve(m_Stats.size());
            for (const auto& [type, _] : m_Stats)
                types.push_back(type);
            return types;
        }

        // Detect memory leaks (objects still alive)
        void DetectLeaks()
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            
            bool leaksDetected = false;
            for (const auto& [typeName, stats] : m_Stats)
            {
                uint64_t alive = stats.currentAlive.load(std::memory_order_relaxed);
                if (alive > 0)
                {
                    leaksDetected = true;
                    #ifdef TRACY_ENABLE
                        std::string msg = "LEAK: " + std::string(typeName) + 
                                         " - " + std::to_string(alive) + " objects still alive!";
                        TracyMessageC(msg.c_str(), msg.size(), 0xFF00FF);
                    #endif
                }
            }

            if (!leaksDetected)
            {
                #ifdef TRACY_ENABLE
                    TracyMessageC("No reference counting leaks detected", 36, 0x00FF00);
                #endif
            }
        }

        // Print comprehensive statistics
        void PrintStatistics() const
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            
            for (const auto& [typeName, stats] : m_Stats)
            {
                uint64_t created = stats.totalCreated.load(std::memory_order_relaxed);
                uint64_t destroyed = stats.totalDestroyed.load(std::memory_order_relaxed);
                uint64_t alive = stats.currentAlive.load(std::memory_order_relaxed);
                uint64_t peak = stats.peakAlive.load(std::memory_order_relaxed);
                uint64_t increments = stats.totalIncrements.load(std::memory_order_relaxed);
                uint64_t decrements = stats.totalDecrements.load(std::memory_order_relaxed);
                uint64_t peakRef = stats.peakRefCount.load(std::memory_order_relaxed);

                printf("\n=== %s ===\n", typeName.c_str());
                printf("  Created:     %llu\n", created);
                printf("  Destroyed:   %llu\n", destroyed);
                printf("  Alive:       %llu\n", alive);
                printf("  Peak Alive:  %llu\n", peak);
                printf("  Increments:  %llu\n", increments);
                printf("  Decrements:  %llu\n", decrements);
                printf("  Peak RefCnt: %llu\n", peakRef);
            }
        }

        // Reset all statistics
        void Reset()
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Stats.clear();
            m_Objects.clear();
        }

    private:
        RefCountTracker() = default;
        ~RefCountTracker() = default;

        RefCountTracker(const RefCountTracker&) = delete;
        RefCountTracker& operator=(const RefCountTracker&) = delete;

        struct ObjectInfo
        {
            std::string typeName;
            uint32_t currentRefCount;
            uint32_t peakRefCount;
        };

        mutable std::mutex m_Mutex;
        std::unordered_map<std::string, RefCountStats> m_Stats;
        std::unordered_map<const void*, ObjectInfo> m_Objects;
    };

#endif // SPARTAN_REFCOUNT_TRACKING_ENABLED

    // Macros that compile to nothing in release builds
    #ifdef SPARTAN_REFCOUNT_TRACKING_ENABLED
        #define SPARTAN_TRACK_REFCOUNT_CREATE(ptr, type) \
            spartan::RefCountTracker::Get().OnObjectCreated(ptr, #type, 0)
        
        #define SPARTAN_TRACK_REFCOUNT_CREATE_NAMED(ptr, typeName) \
            spartan::RefCountTracker::Get().OnObjectCreated(ptr, typeName, 0)
        
        #define SPARTAN_TRACK_REFCOUNT_INCREMENT(ptr, newCount) \
            spartan::RefCountTracker::Get().OnRefIncrement(ptr, newCount)
        
        #define SPARTAN_TRACK_REFCOUNT_DECREMENT(ptr, newCount) \
            spartan::RefCountTracker::Get().OnRefDecrement(ptr, newCount)
        
        #define SPARTAN_TRACK_REFCOUNT_DESTROY(ptr) \
            spartan::RefCountTracker::Get().OnObjectDestroyed(ptr)
        
        #define SPARTAN_TRACK_REFCOUNT_DETECT_LEAKS() \
            spartan::RefCountTracker::Get().DetectLeaks()
        
        #define SPARTAN_TRACK_REFCOUNT_PRINT_STATS() \
            spartan::RefCountTracker::Get().PrintStatistics()
        
        #define SPARTAN_TRACK_REFCOUNT_RESET() \
            spartan::RefCountTracker::Get().Reset()
    #else
        #define SPARTAN_TRACK_REFCOUNT_CREATE(ptr, type)
        #define SPARTAN_TRACK_REFCOUNT_CREATE_NAMED(ptr, typeName)
        #define SPARTAN_TRACK_REFCOUNT_INCREMENT(ptr, newCount)
        #define SPARTAN_TRACK_REFCOUNT_DECREMENT(ptr, newCount)
        #define SPARTAN_TRACK_REFCOUNT_DESTROY(ptr)
        #define SPARTAN_TRACK_REFCOUNT_DETECT_LEAKS()
        #define SPARTAN_TRACK_REFCOUNT_PRINT_STATS()
        #define SPARTAN_TRACK_REFCOUNT_RESET()
    #endif

} // namespace spartan


