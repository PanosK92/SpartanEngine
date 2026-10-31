/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>

namespace spartan
{
    // Dedicated queues keep driver/DXC work out of the frame's general-purpose task pool.
    // A single pipeline worker also serializes access to native pipeline caches/libraries.
    class RHI_Compiler
    {
    public:
        static RHI_Compiler& Shaders() { static RHI_Compiler queue; return queue; }
        static RHI_Compiler& Pipelines() { static RHI_Compiler queue; return queue; }

        std::future<void> Submit(std::function<void()> function)
        {
            std::packaged_task<void()> task(std::move(function));
            auto result = task.get_future();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_tasks.push_back(std::move(task));
                ++m_pending;
            }
            m_wake.notify_one();
            return result;
        }

        // Only teardown waits. Rendering polls the published result instead.
        void Flush() { Submit([] {}).wait(); }
        bool IsIdle() const { return m_pending.load() == 0; }

        ~RHI_Compiler()
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stopping = true;
            }
            m_wake.notify_one();
            m_worker.join();
        }

    private:
        RHI_Compiler() : m_worker([this]()
        {
            for (;;)
            {
                std::packaged_task<void()> task;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_wake.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });
                    if (m_tasks.empty() && m_stopping)
                        return;
                    task = std::move(m_tasks.front());
                    m_tasks.pop_front();
                }
                task();
                --m_pending;
            }
        }) {}

        std::atomic<uint32_t> m_pending = 0;
        std::mutex m_mutex;
        std::condition_variable m_wake;
        std::deque<std::packaged_task<void()>> m_tasks;
        bool m_stopping = false;
        std::thread m_worker;
    };
}
