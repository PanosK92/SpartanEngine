/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace spartan
{
    // Dedicated queues keep driver/DXC work out of the frame's general-purpose task pool.
    // Shaders compile in parallel, each worker owns its own dxc instance.
    // A single pipeline worker serializes access to native pipeline caches/libraries.
    class RHI_Compiler
    {
    public:
        static RHI_Compiler& Shaders() { static RHI_Compiler queue(std::clamp(std::thread::hardware_concurrency() / 2u, 1u, 8u)); return queue; }
        static RHI_Compiler& Pipelines() { static RHI_Compiler queue(1); return queue; }

        // work submitted while the queue is busy joins the running batch, so the count only resets once it drains
        struct Progress
        {
            uint32_t completed = 0;
            uint32_t submitted = 0;
        };

        std::future<void> Submit(std::function<void()> function)
        {
            std::packaged_task<void()> task(std::move(function));
            auto result = task.get_future();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_pending == 0)
                {
                    m_batch_completed = 0;
                    m_batch_submitted = 0;
                }
                m_tasks.push_back(std::move(task));
                ++m_batch_submitted;
                ++m_pending;
            }
            m_wake.notify_one();
            return result;
        }

        // Only teardown waits. Rendering polls the published result instead.
        void Flush()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_idle.wait(lock, [this] { return m_pending == 0; });
        }

        bool IsIdle() const { return m_pending.load() == 0; }

        Progress GetProgress() const
        {
            Progress progress;
            progress.submitted = m_batch_submitted.load();
            progress.completed = std::min(m_batch_completed.load(), progress.submitted);
            return progress;
        }

        ~RHI_Compiler()
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stopping = true;
            }
            m_wake.notify_all();
            for (std::thread& worker : m_workers)
            {
                worker.join();
            }
        }

    private:
        explicit RHI_Compiler(const uint32_t worker_count)
        {
            for (uint32_t i = 0; i < worker_count; i++)
            {
                m_workers.emplace_back([this]()
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
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            ++m_batch_completed;
                            --m_pending;
                        }
                        m_idle.notify_all();
                    }
                });
            }
        }

        std::atomic<uint32_t> m_pending         = 0;
        std::atomic<uint32_t> m_batch_completed = 0;
        std::atomic<uint32_t> m_batch_submitted = 0;
        std::mutex m_mutex;
        std::condition_variable m_wake;
        std::condition_variable m_idle;
        std::deque<std::packaged_task<void()>> m_tasks;
        bool m_stopping = false;
        std::vector<std::thread> m_workers;
    };
}
