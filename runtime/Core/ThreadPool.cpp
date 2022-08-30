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
    // Stats
    static uint32_t thread_count                 = 0;
    static uint32_t thread_count_support         = 0;
    static atomic<uint32_t> working_thread_count = 0;

    // Sync objects
    static mutex mutex_tasks;
    static condition_variable condition_var;

    // Threads
    static vector<thread> threads;
    static unordered_map<thread::id, string> thread_names;

    // Tasks
    static deque<Task> tasks;

    // Misc
    static bool is_stopping;

    static void thread_loop()
    {
        while (true)
        {
            // Lock tasks mutex
            unique_lock<mutex> lock(mutex_tasks);

            // Check condition on notification
            condition_var.wait(lock, [] { return !tasks.empty() || is_stopping; });

            // If m_stopping is true, it's time to shut everything down
            if (is_stopping && tasks.empty())
                return;

            // Get next task in the queue.
            Task task = tasks.front();

            // Remove it from the queue.
            tasks.pop_front();

            // Unlock the mutex
            lock.unlock();

            // Execute the task.
            working_thread_count++;
            task();
            working_thread_count--;
        }
    }

    void ThreadPool::Initialize()
    {
        is_stopping                         = false;
        thread_count_support                = thread::hardware_concurrency();
        thread_count                        = thread_count_support - 1; // exclude the main (this) thread
        thread_names[this_thread::get_id()] = "main";

        for (uint32_t i = 0; i < thread_count; i++)
        {
            threads.emplace_back(thread(&thread_loop));
            thread_names[threads.back().get_id()] = "worker_" + to_string(i);
        }

        SP_LOG_INFO("%d threads have been created", thread_count);
    }

    void ThreadPool::Shutdown()
    {
        Flush(true);

        // Put unique lock on task mutex.
        unique_lock<mutex> lock(mutex_tasks);

        // Set termination flag to true.
        is_stopping = true;

        // Unlock the mutex
        lock.unlock();

        // Wake up all threads.
        condition_var.notify_all();

        // Join all threads.
        for (auto& thread : threads)
        {
            thread.join();
        }

        // Empty worker threads.
        threads.clear();
    }

    void ThreadPool::AddTask(Task&& task)
    {
        if (GetIdleThreadCount() == 0)
        {
            SP_LOG_WARNING("No available threads, function will execute in the calling thread");
            task();
            return;
        }

        // Lock tasks mutex
        unique_lock<mutex> lock(mutex_tasks);

        // Save the task
        tasks.emplace_back(bind(forward<Task>(task)));

        // Unlock the mutex
        lock.unlock();

        // Wake up a thread
        condition_var.notify_one();
    }

    void ThreadPool::AddRangedFunction(function<void(uint32_t work_index_start, uint32_t work_index_end)>&& function, uint32_t work_count)
    {
        uint32_t available_threads = GetIdleThreadCount();
        vector<bool> tasks_done    = vector<bool>(available_threads, false);
        const uint32_t task_count  = available_threads + 1; // plus one for the current thread

        uint32_t start = 0;
        uint32_t end   = 0;
        for (uint32_t i = 0; i < available_threads; i++)
        {
            start = (work_count / task_count) * i;
            end   = start + (work_count / task_count);

            // Kick off task
            AddTask([&function, &tasks_done, i, start, end] { function(start, end); tasks_done[i] = true; });
        }

        // Do last task in the current thread
        function(end, work_count);

        // Wait till the threads are done
        uint32_t tasks = 0;
        while (tasks != tasks_done.size())
        {
            tasks = 0;
            for (const bool job_done : tasks_done)
            {
                tasks += job_done ? 1 : 0;
            }

            this_thread::sleep_for(chrono::milliseconds(16));
        }
    }

    void ThreadPool::Flush(bool remove_queued /*= false*/)
    {
        // Clear any queued tasks
        if (remove_queued)
        {
            tasks.clear();
        }

        // If so, wait for them
        while (AreTasksRunning())
        {
            this_thread::sleep_for(chrono::milliseconds(16));
        }
    }

    uint32_t ThreadPool::GetThreadCount()          { return thread_count; }
    uint32_t ThreadPool::GetSupportedThreadCount() { return thread_count_support; }
    uint32_t ThreadPool::GetWorkingThreadCount()   { return working_thread_count; }
    uint32_t ThreadPool::GetIdleThreadCount()      { return thread_count - working_thread_count; }
    bool ThreadPool::AreTasksRunning()             { return GetIdleThreadCount() != GetThreadCount(); }
}
