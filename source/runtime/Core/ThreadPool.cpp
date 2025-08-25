/*
Copyright(c) 2015-2025 Panos Karabelas

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
        // Stats
        static uint32_t thread_count                 = 0;
        static atomic<uint32_t> working_thread_count = 0;

        // Sync objects
        static mutex mutex_tasks;
        static condition_variable condition_var;

        // Threads
        static vector<thread> threads;

        // Tasks
        static deque<Task> tasks;

        // Misc
        static bool is_stopping;
    }

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
        is_stopping = false;

        uint32_t core_count = thread::hardware_concurrency() / 2;  // assume physical cores
        thread_count        = min(core_count * 2, core_count + 4); // 2x for I/O-bound, cap at core_count + 4

        for (uint32_t i = 0; i < thread_count; i++)
        {
            threads.emplace_back(thread(&thread_loop));
        }

        SP_LOG_INFO("%d threads have been created", thread_count);
    }

    void ThreadPool::Shutdown()
    {
        Flush(true);

        // put unique lock on task mutex
        unique_lock<mutex> lock(mutex_tasks);

        // set termination flag to true
        is_stopping = true;

        // unlock the mutex
        lock.unlock();

        // Wake up all threads
        condition_var.notify_all();

        // join all threads
        for (auto& thread : threads)
        {
            thread.join();
        }

        // empty worker threads
        threads.clear();
    }

    future<void> ThreadPool::AddTask(Task&& task)
    {
        // create a packaged task that will give us a future
        auto packaged_task = make_shared<std::packaged_task<void()>>(std::forward<Task>(task));
        
        // get the future before we move the packaged_task into the lambda
        future<void> future = packaged_task->get_future();
        
        // lock tasks mutex
        unique_lock<mutex> lock(mutex_tasks);
        
        // save the task - wrap the packaged_task in a lambda that will execute it
        tasks.emplace_back([packaged_task]()
        {
            (*packaged_task)();
        });
        
        // unlock the mutex
        lock.unlock();
        
        // wake up a thread
        condition_var.notify_one();
        
        // return the future that can be used to wait for task completion
        return future;
    }

    void ThreadPool::ParallelLoop(function<void(uint32_t work_index_start, uint32_t work_index_end)>&& function, const uint32_t work_total)
    {
        SP_ASSERT_MSG(work_total > 0, "A parallel loop must have a work_total of at least 1");

        // ensure we have available threads, if not, execute the function in the caller thread
        uint32_t available_threads = GetIdleThreadCount();
        if (available_threads == 0)
        {
            function(0, work_total);
            return;
        }

        uint32_t work_per_thread   = work_total / available_threads;
        uint32_t work_remainder    = work_total % available_threads;
        uint32_t work_index        = 0;
        atomic<uint32_t> work_done = 0;
        condition_variable cv;
        mutex cv_m;

        // split work into multiple tasks
        while (work_index < work_total)
        {
            uint32_t work_to_do = work_per_thread;

            // if the work doesn't divide evenly across threads, add the remainder work to the first thread.
            if (work_remainder != 0)
            {
                work_to_do     += work_remainder;
                work_remainder = 0;
            }

            AddTask([&function, &work_done, &cv, work_index, work_to_do]()
            {
                function(work_index, work_index + work_to_do);
                work_done += work_to_do;

                cv.notify_one(); // notify that a thread has finished its work
            });

            work_index += work_to_do;
        }

        // wait for threads to finish work
        unique_lock<mutex> lk(cv_m);
        cv.wait(lk, [&]() { return work_done == work_total; });
    }

    void ThreadPool::Flush(bool remove_queued /*= false*/)
    {
        // clear any queued tasks
        if (remove_queued)
        {
            tasks.clear();
        }

        // wait for any tasks to complete
        while (AreTasksRunning())
        {
            this_thread::sleep_for(chrono::milliseconds(16));
        }
    }

    uint32_t ThreadPool::GetThreadCount()        { return thread_count; }
    uint32_t ThreadPool::GetWorkingThreadCount() { return working_thread_count; }
    uint32_t ThreadPool::GetIdleThreadCount()    { return thread_count - working_thread_count; }
    bool ThreadPool::AreTasksRunning()           { return GetIdleThreadCount() != GetThreadCount(); }
}
