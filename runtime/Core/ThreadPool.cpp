/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES =========
#include "pch.h"
#include "ThreadPool.h"
//====================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    uint32_t ThreadPool::m_thread_count                      = 0;
    uint32_t ThreadPool::m_thread_count_support              = 0;
    std::atomic<uint32_t> ThreadPool::m_working_thread_count = 0;
    std::vector<std::thread> ThreadPool::m_threads;
    std::deque<std::shared_ptr<Task>> ThreadPool::m_tasks;
    std::mutex ThreadPool::m_mutex_tasks;
    std::condition_variable ThreadPool::m_condition_var;
    std::unordered_map<std::thread::id, std::string> ThreadPool::m_thread_names;
    bool ThreadPool::m_stopping;

    void ThreadPool::Initialize()
    {
        m_stopping                            = false;
        m_thread_count_support                = thread::hardware_concurrency();
        m_thread_count                        = m_thread_count_support - 1; // exclude the main (this) thread
        m_thread_names[this_thread::get_id()] = "main";

        for (uint32_t i = 0; i < m_thread_count; i++)
        {
            m_threads.emplace_back(thread(&ThreadPool::ThreadLoop));
            m_thread_names[m_threads.back().get_id()] = "worker_" + to_string(i);
        }

        SP_LOG_INFO("%d threads have been created", m_thread_count);
    }

    void ThreadPool::Shutdown()
    {
        Flush(true);

        // Put unique lock on task mutex.
        unique_lock<mutex> lock(m_mutex_tasks);

        // Set termination flag to true.
        m_stopping = true;

        // Unlock the mutex
        lock.unlock();

        // Wake up all threads.
        m_condition_var.notify_all();

        // Join all threads.
        for (auto& thread : m_threads)
        {
            thread.join();
        }

        // Empty worker threads.
        m_threads.clear();
    }

    void ThreadPool::Flush(bool remove_queued /*= false*/)
    {
        // Clear any queued tasks
        if (remove_queued)
        {
            m_tasks.clear();
        }

        // If so, wait for them
        while (AreTasksRunning())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    void ThreadPool::ThreadLoop()
    {
        shared_ptr<Task> task;

        while (true)
        {
            // Lock tasks mutex
            unique_lock<mutex> lock(m_mutex_tasks);

            // Check condition on notification
            m_condition_var.wait(lock, []{ return !m_tasks.empty() || m_stopping; });

            // If m_stopping is true, it's time to shut everything down
            if (m_stopping && m_tasks.empty())
                return;

            // Get next task in the queue.
            task = m_tasks.front();

            // Remove it from the queue.
            m_tasks.pop_front();

            // Unlock the mutex
            lock.unlock();

            // Execute the task.
            m_working_thread_count++;
            task->Execute();
            m_working_thread_count--;
        }
    }
}
