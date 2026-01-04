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
        // stats
        static uint32_t thread_count = 0;
        static atomic<uint32_t> working_thread_count = 0;

        // sync objects
        static mutex mutex_tasks;
        static condition_variable condition_var;

        // threads
        static vector<thread> threads;

        // tasks
        static deque<Task> tasks;

        // misc
        static bool is_stopping = false;
    }

    static void thread_loop()
    {
        while (true)
        {
            Task task;
            {
                unique_lock<mutex> lock(mutex_tasks);

                condition_var.wait(lock, [] { return !tasks.empty() || is_stopping; });

                if (is_stopping && tasks.empty())
                    return;

                // move the task out of the queue
                task = std::move(tasks.front());
                tasks.pop_front();
            }

            // execute the task outside the lock
            working_thread_count.fetch_add(1, memory_order_acq_rel);
            try
            {
                task();
            }
            catch (...)
            {
                // swallow exceptions to avoid terminating the thread
            }
            working_thread_count.fetch_sub(1, memory_order_acq_rel);
        }
    }

    void ThreadPool::Initialize()
    {
        // reset stopping flag in case of reinitialization attempts.
        is_stopping = false;

        // determine core count safely
        uint32_t hw_conc = thread::hardware_concurrency();
        if (hw_conc == 0)
        {
            hw_conc = 4; // fallback
        }

        uint32_t core_count = max(1u, hw_conc / 2);         // assume physical cores
        thread_count = min(core_count * 2, core_count + 4); // 2x for I/O bound, cap at core_count + 4

        // create threads
        threads.reserve(thread_count);
        for (uint32_t i = 0; i < thread_count; i++)
        {
            threads.emplace_back(thread(&thread_loop));
        }

        SP_LOG_INFO("%d threads have been created", thread_count);
    }

    void ThreadPool::Shutdown()
    {
        // ensure queued tasks are flushed and optionally removed by caller
        Flush(true);

        {
            unique_lock<mutex> lock(mutex_tasks);
            is_stopping = true;
        }

        // wake up all threads so they can exit
        condition_var.notify_all();

        for (auto& t : threads)
        {
            if (t.joinable())
                t.join();
        }

        threads.clear();

        // reset counters
        working_thread_count.store(0, memory_order_relaxed);
        thread_count = 0;
    }

    future<void> ThreadPool::AddTask(Task&& task)
    {
        // packaged_task to get a future back
        auto packaged_task = make_shared<std::packaged_task<void()>>(std::forward<Task>(task));
        future<void> fut = packaged_task->get_future();

        {
            unique_lock<mutex> lock(mutex_tasks);

            // reject tasks if the pool is stopping to avoid hanging futures
            if (is_stopping)
            {
                SP_LOG_WARNING("ThreadPool::AddTask() called while pool is stopping, task will not be executed");
                return fut;
            }

            // wrap the packaged task execution in a simple lambda that will be stored in the deque
            tasks.emplace_back([packaged_task]()
            {
                try
                {
                    (*packaged_task)();
                }
                catch (...)
                {
                    // rethrow inside packaged_task will be captured by future
                    throw;
                }
            });
        }

        // notify outside the lock to avoid waking a thread that immediately blocks
        condition_var.notify_one();

        return fut;
    }

    void ThreadPool::ParallelLoop(function<void(uint32_t, uint32_t)>&& function, const uint32_t work_total)
    {
        // ensure there is at least one unit of work
        SP_ASSERT_MSG(work_total > 0, "a parallel loop must have a work_total of at least 1");

        // if all worker threads are busy or there are no threads,
        // run the work serially on the calling thread to avoid deadlock
        if (GetWorkingThreadCount() == thread_count || threads.empty())
        {
            function(0, work_total);
            return;
        }

        // decide how many workers will be used (at least 1)
        uint32_t workers = max(1u, thread_count);

        // divide the work as evenly as possible among workers
        uint32_t base_work = work_total / workers; // minimum amount of work per worker
        uint32_t remainder = work_total % workers; // leftover work distributed one per worker

        // store futures so we can wait for all tasks to complete
        vector<future<void>> futures;
        futures.reserve(workers);

        uint32_t work_index = 0;
        for (uint32_t i = 0; i < workers && work_index < work_total; ++i)
        {
            // each worker gets base_work, and if remainder > 0, give one extra unit of work
            uint32_t work_to_do = base_work + (remainder > 0 ? 1u : 0u);
            if (remainder > 0)
            {
                --remainder;
            }

            // define the start and end of this worker's range
            uint32_t start = work_index;
            uint32_t end   = work_index + work_to_do;

            // enqueue the task into the thread pool
            futures.emplace_back(AddTask([fn = function, start, end]() mutable { fn(start, end); }));

            // move to the next block of work
            work_index = end;
        }

        // wait for all worker tasks to finish
        for (auto& f : futures)
        {
            f.get();
        }
    }

    void ThreadPool::Flush(bool remove_queued /*= false*/)
    {
        if (remove_queued)
        {
            unique_lock<mutex> lock(mutex_tasks);
            tasks.clear();
        }

        // wait until all queued tasks are consumed and no threads are working
        while (true)
        {
            {
                unique_lock<mutex> lock(mutex_tasks);
                if (tasks.empty() && working_thread_count.load(memory_order_acquire) == 0)
                    break;
            }
            this_thread::sleep_for(chrono::milliseconds(1));
        }
    }

    uint32_t ThreadPool::GetThreadCount() { return thread_count; }
    uint32_t ThreadPool::GetWorkingThreadCount() { return working_thread_count.load(memory_order_acquire); }
    uint32_t ThreadPool::GetIdleThreadCount() { return (thread_count > GetWorkingThreadCount()) ? (thread_count - GetWorkingThreadCount()) : 0; }

    bool ThreadPool::AreTasksRunning()
    {
        unique_lock<mutex> lock(mutex_tasks);
        return !tasks.empty() || working_thread_count.load(memory_order_acquire) != 0;
    }
}
