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

//= INCLUDES =========
#include "pch.h"
#include "ThreadPool.h"
//====================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        uint32_t thread_count           = 0;
        atomic<uint32_t> working_count  = 0;
        atomic<uint32_t> pending_count  = 0;
        bool stopping                   = false;

        mutex task_mutex;
        condition_variable task_cv;      // signaled when tasks are added or stopping
        condition_variable idle_cv;      // signaled when a task completes

        vector<thread> threads;
        deque<Task> tasks;

        // track if current thread is a worker thread to detect nested ParallelLoop calls
        thread_local bool is_worker_thread = false;
    }

    static void thread_loop()
    {
        is_worker_thread = true;

        while (true)
        {
            Task task;
            {
                unique_lock<mutex> lock(task_mutex);

                task_cv.wait(lock, [] { return !tasks.empty() || stopping; });

                if (stopping && tasks.empty())
                    return;

                task = std::move(tasks.front());
                tasks.pop_front();
            }

            working_count.fetch_add(1, memory_order_relaxed);

            // execute task - exceptions are handled by packaged_task if one is used
            task();

            working_count.fetch_sub(1, memory_order_relaxed);
            pending_count.fetch_sub(1, memory_order_relaxed);

            // wake up any thread waiting in flush()
            idle_cv.notify_all();
        }
    }

    void ThreadPool::Initialize()
    {
        stopping = false;

        uint32_t hw_threads = thread::hardware_concurrency();
        if (hw_threads == 0)
            hw_threads = 4;

        // assume half are physical cores, then scale for mixed workloads
        uint32_t core_count = max(1u, hw_threads / 2);
        thread_count        = min(core_count * 2, core_count + 4);

        threads.reserve(thread_count);
        for (uint32_t i = 0; i < thread_count; i++)
        {
            threads.emplace_back(thread_loop);
        }

        SP_LOG_INFO("%d threads have been created", thread_count);
    }

    void ThreadPool::Shutdown()
    {
        Flush(true);

        {
            lock_guard<mutex> lock(task_mutex);
            stopping = true;
        }

        task_cv.notify_all();

        for (thread& t : threads)
        {
            if (t.joinable())
                t.join();
        }

        threads.clear();
        working_count.store(0, memory_order_relaxed);
        pending_count.store(0, memory_order_relaxed);
        thread_count = 0;
    }

    future<void> ThreadPool::AddTask(Task&& task)
    {
        auto packaged = make_shared<packaged_task<void()>>(std::forward<Task>(task));
        future<void> result = packaged->get_future();

        {
            lock_guard<mutex> lock(task_mutex);

            if (stopping)
            {
                SP_LOG_WARNING("ThreadPool::AddTask() called while pool is stopping");
                return result;
            }

            pending_count.fetch_add(1, memory_order_relaxed);
            tasks.emplace_back([packaged]() { (*packaged)(); });
        }

        task_cv.notify_one();
        return result;
    }

    void ThreadPool::ParallelLoop(function<void(uint32_t, uint32_t)>&& function, const uint32_t work_total)
    {
        SP_ASSERT_MSG(work_total > 0, "parallel loop requires work_total > 0");

        // no threads available - run on calling thread
        if (threads.empty())
        {
            function(0, work_total);
            return;
        }

        // when called from a worker thread, check if we have idle workers available
        // this allows nested parallelism when possible while preventing deadlock
        uint32_t available_workers = thread_count;
        if (is_worker_thread)
        {
            uint32_t currently_working = working_count.load(memory_order_relaxed);
            if (currently_working >= thread_count)
            {
                // all workers busy - run sequentially to prevent deadlock
                function(0, work_total);
                return;
            }
            // use only idle workers to avoid deadlock
            available_workers = thread_count - currently_working;
        }

        // limit workers to available count and work count
        uint32_t workers   = min(available_workers, work_total);
        uint32_t base_work = work_total / workers;
        uint32_t remainder = work_total % workers;

        vector<future<void>> futures;
        futures.reserve(workers);

        uint32_t work_index = 0;
        for (uint32_t i = 0; i < workers; ++i)
        {
            uint32_t work_count = base_work + (i < remainder ? 1u : 0u);
            uint32_t start      = work_index;
            uint32_t end        = work_index + work_count;

            futures.emplace_back(AddTask([fn = function, start, end]() { fn(start, end); }));
            work_index = end;
        }

        for (future<void>& f : futures)
        {
            f.get();
        }
    }

    void ThreadPool::Flush(bool remove_queued)
    {
        if (remove_queued)
        {
            lock_guard<mutex> lock(task_mutex);
            uint32_t removed = static_cast<uint32_t>(tasks.size());
            tasks.clear();
            pending_count.fetch_sub(removed, memory_order_relaxed);
        }

        // wait for all in-flight work to complete using condition variable (no spin)
        unique_lock<mutex> lock(task_mutex);
        idle_cv.wait(lock, [] {
            return tasks.empty() && working_count.load(memory_order_relaxed) == 0;
        });
    }

    uint32_t ThreadPool::GetThreadCount()
    {
        return thread_count;
    }

    uint32_t ThreadPool::GetWorkingThreadCount()
    {
        return working_count.load(memory_order_relaxed);
    }

    uint32_t ThreadPool::GetIdleThreadCount()
    {
        uint32_t working = working_count.load(memory_order_relaxed);
        return (thread_count > working) ? (thread_count - working) : 0;
    }

    bool ThreadPool::AreTasksRunning()
    {
        return pending_count.load(memory_order_relaxed) > 0;
    }
}
