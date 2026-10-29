/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================
#include "pch.h"
#include "ThreadPool.h"
#include "../error_handling/CrashHandler.h"
#include <thread>
#include <condition_variable>
#include <deque>
#include <chrono>
#include <immintrin.h>
//============================

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
        // mirrors tasks.size() so idle workers can poll for work without taking the mutex
        atomic<uint32_t> queued_count = 0;
        atomic<bool> stopping_flag    = false;

        thread_local bool is_worker_thread = false;
        // nested ParallelLoop from inside a pool task must run inline, otherwise workers
        // wait on futures that can only run on the same exhausted pool and the load freezes
        thread_local uint32_t parallel_depth = 0;

        void complete_task()
        {
            // Flush tests this predicate while holding task_mutex. Updating it
            // under the same lock prevents the last completion notification
            // from landing between its predicate check and entering the wait.
            {
                lock_guard<mutex> lock(task_mutex);
                working_count.fetch_sub(1, memory_order_relaxed);
                pending_count.fetch_sub(1, memory_order_relaxed);
            }
            idle_cv.notify_all();
        }
    }

    static void thread_loop()
    {
        CrashHandler::InitializeThread();
        is_worker_thread = true;

        while (true)
        {
            // a frame issues parallel loops back to back, waking from the condition variable costs
            // tens of microseconds per thread, so stay awake briefly and pick the next loop up at once
            if (queued_count.load(memory_order_relaxed) == 0)
            {
                const auto spin_end = chrono::steady_clock::now() + chrono::microseconds(100);
                for (uint32_t spin = 0; queued_count.load(memory_order_relaxed) == 0 && !stopping_flag.load(memory_order_relaxed); ++spin)
                {
                    _mm_pause();
                    if ((spin & 63) == 63 && chrono::steady_clock::now() >= spin_end)
                    {
                        break;
                    }
                }
            }

            Task task;
            {
                unique_lock<mutex> lock(task_mutex);

                task_cv.wait(lock, [] { return !tasks.empty() || stopping; });

                if (stopping && tasks.empty())
                {
                    return;
                }

                task = std::move(tasks.front());
                tasks.pop_front();
                queued_count.fetch_sub(1, memory_order_relaxed);
                // count work before releasing the mutex so flush cannot see empty+idle in the pop gap
                working_count.fetch_add(1, memory_order_relaxed);
            }

            // execute task - exceptions are handled by packaged_task if one is used
            task();

            complete_task();
        }
    }

    void ThreadPool::Initialize()
    {
        stopping = false;
        stopping_flag.store(false, memory_order_relaxed);

        uint32_t hw_threads = thread::hardware_concurrency();
        if (hw_threads == 0)
        {
            hw_threads = 4;
        }

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
            stopping_flag.store(true, memory_order_relaxed);
        }

        task_cv.notify_all();

        for (thread& t : threads)
        {
            if (t.joinable())
            {
                t.join();
            }
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
            queued_count.fetch_add(1, memory_order_relaxed);
        }

        task_cv.notify_one();
        return result;
    }

    void ThreadPool::ParallelLoop(function<void(uint32_t, uint32_t)>&& work_fn, const uint32_t work_total)
    {
        SP_ASSERT_MSG(work_total > 0, "parallel loop requires work_total > 0");

        // no threads available - run on calling thread
        if (threads.empty())
        {
            work_fn(0, work_total);
            return;
        }

        // nested ParallelLoop from inside a chunk must run inline, scheduling more pool
        // tasks while every worker waits on futures freezes world load (dreamcore at ~89%)
        if (parallel_depth > 0)
        {
            work_fn(0, work_total);
            return;
        }

        struct depth_scope
        {
            depth_scope()  { parallel_depth++; }
            ~depth_scope() { parallel_depth--; }
        };

        const uint32_t workers = min(thread_count, work_total);
        depth_scope caller_depth;
        if (workers == 1)
        {
            work_fn(0, work_total);
            return;
        }

        // chunks are claimed from a shared counter, the caller starts at once and helpers join as they wake,
        // so a slow wake costs only the chunks that helper would have taken instead of stalling the whole loop
        struct LoopState
        {
            function<void(uint32_t, uint32_t)> fn;
            uint32_t work_total  = 0;
            uint32_t chunk_count = 0;
            atomic<uint32_t> next_chunk = 0;
            atomic<uint32_t> done_chunk = 0;
            mutex failure_mutex;
            exception_ptr failure;
        };
        const shared_ptr<LoopState> state = make_shared<LoopState>();
        state->fn          = std::move(work_fn);
        state->work_total  = work_total;
        state->chunk_count = min(work_total, workers * 4);

        // a helper that wakes after every chunk is claimed returns without touching fn,
        // the caller waits for every claimed chunk, so fn and its captures outlive any call
        auto run_chunks = [](LoopState& loop)
        {
            while (true)
            {
                const uint32_t chunk = loop.next_chunk.fetch_add(1, memory_order_relaxed);
                if (chunk >= loop.chunk_count)
                {
                    return;
                }
                const uint32_t first = static_cast<uint32_t>(static_cast<uint64_t>(loop.work_total) * chunk / loop.chunk_count);
                const uint32_t last  = static_cast<uint32_t>(static_cast<uint64_t>(loop.work_total) * (chunk + 1) / loop.chunk_count);
                try
                {
                    loop.fn(first, last);
                }
                catch (...)
                {
                    lock_guard<mutex> lock(loop.failure_mutex);
                    if (!loop.failure)
                    {
                        loop.failure = current_exception();
                    }
                }
                loop.done_chunk.fetch_add(1, memory_order_release);
            }
        };

        const uint32_t helpers = workers - 1;
        {
            lock_guard<mutex> lock(task_mutex);
            if (!stopping)
            {
                pending_count.fetch_add(helpers, memory_order_relaxed);
                for (uint32_t i = 0; i < helpers; ++i)
                {
                    // chunks inherit nesting depth so ParallelLoop inside them stays inline
                    tasks.emplace_back([state, run_chunks]()
                    {
                        depth_scope chunk_depth;
                        run_chunks(*state);
                    });
                }
                queued_count.fetch_add(helpers, memory_order_relaxed);
            }
        }
        if (helpers >= thread_count)
        {
            task_cv.notify_all();
        }
        else
        {
            for (uint32_t i = 0; i < helpers; ++i)
            {
                task_cv.notify_one();
            }
        }

        run_chunks(*state);

        // the remaining chunks are already running on other threads, they finish within one chunk of work
        for (uint32_t spin = 0; state->done_chunk.load(memory_order_acquire) < state->chunk_count; ++spin)
        {
            if (spin < 4096)
            {
                _mm_pause();
            }
            else
            {
                this_thread::yield();
            }
        }

        if (state->failure)
        {
            rethrow_exception(state->failure);
        }
    }

    void ThreadPool::ParallelCopy(void* destination, const void* source, size_t size)
    {
        // below a few megabytes the task round trip costs more than the copy
        constexpr size_t parallel_min_size = 2ull * 1024 * 1024;
        constexpr size_t chunk_size        = 512ull * 1024;
        if (size < parallel_min_size || threads.empty() || is_worker_thread || parallel_depth > 0)
        {
            memcpy(destination, source, size);
            return;
        }

        const uint32_t chunk_count = static_cast<uint32_t>((size + chunk_size - 1) / chunk_size);
        ParallelLoop([destination, source, size](uint32_t first, uint32_t last)
        {
            const size_t begin = static_cast<size_t>(first) * chunk_size;
            const size_t end   = min(static_cast<size_t>(last) * chunk_size, size);
            memcpy(static_cast<uint8_t*>(destination) + begin, static_cast<const uint8_t*>(source) + begin, end - begin);
        }, chunk_count);
    }

    void ThreadPool::Flush(bool remove_queued)
    {
        if (remove_queued)
        {
            lock_guard<mutex> lock(task_mutex);
            uint32_t removed = static_cast<uint32_t>(tasks.size());
            tasks.clear();
            queued_count.store(0, memory_order_relaxed);
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
