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
#include "../math/Intrinsics.h"
//============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        uint32_t thread_count = 0;
        bool stopping         = false; // guarded by task_mutex

        mutex task_mutex;
        condition_variable task_cv; // signaled when tasks are added or the pool stops
        condition_variable idle_cv; // signaled when pending_count reaches zero
        vector<thread> threads;
        deque<Task> tasks;

        // queued plus running tasks, flush waits for zero, only incremented under task_mutex
        atomic<uint32_t> pending_count = 0;
        // mirrors tasks.size() so idle workers can poll for work without taking the mutex, only written under task_mutex
        atomic<uint32_t> queued_count  = 0;
        atomic<uint32_t> working_count = 0;
        atomic<bool> stopping_flag     = false;

        thread_local bool is_worker_thread   = false;
        // nested ParallelLoop runs inline, the outer loop already occupies every worker
        thread_local uint32_t parallel_depth = 0;

        struct depth_scope
        {
            depth_scope()  { parallel_depth++; }
            ~depth_scope() { parallel_depth--; }
        };

        // caller holds task_mutex
        void push_locked(Task&& task)
        {
            tasks.emplace_back(std::move(task));
            queued_count.fetch_add(1, memory_order_relaxed);
            pending_count.fetch_add(1, memory_order_relaxed);
        }

        // caller must not hold task_mutex
        void release_pending(uint32_t count)
        {
            if (pending_count.fetch_sub(count, memory_order_acq_rel) == count)
            {
                // a flusher may sit between its predicate check and its wait, passing through the mutex orders this wake after that wait
                {
                    lock_guard<mutex> lock(task_mutex);
                }
                idle_cv.notify_all();
            }
        }

        void thread_loop()
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
                    for (uint32_t spin = 1; queued_count.load(memory_order_relaxed) == 0 && !stopping_flag.load(memory_order_relaxed); ++spin)
                    {
                        _mm_pause();
                        if ((spin & 63) == 0 && chrono::steady_clock::now() >= spin_end)
                        {
                            break;
                        }
                    }
                }

                Task task;
                {
                    unique_lock<mutex> lock(task_mutex);
                    task_cv.wait(lock, [] { return !tasks.empty() || stopping; });

                    // queued work is drained before exiting
                    if (tasks.empty())
                    {
                        return;
                    }

                    task = std::move(tasks.front());
                    tasks.pop_front();
                    queued_count.fetch_sub(1, memory_order_relaxed);
                }

                working_count.fetch_add(1, memory_order_relaxed);
                task();
                // captures must be gone before the task counts as complete, flush callers tear down what they reference
                task = nullptr;
                working_count.fetch_sub(1, memory_order_relaxed);

                release_pending(1);
            }
        }
    }

    void ThreadPool::Initialize()
    {
        if (!threads.empty())
        {
            return;
        }

        stopping = false;
        stopping_flag.store(false, memory_order_relaxed);

        uint32_t hw_threads = thread::hardware_concurrency();
        if (hw_threads == 0)
        {
            hw_threads = 4;
        }

        // assume half are physical cores, then scale for mixed workloads
        const uint32_t core_count = max(1u, hw_threads / 2);
        thread_count              = min(core_count * 2, core_count + 4);

        threads.reserve(thread_count);
        for (uint32_t i = 0; i < thread_count; i++)
        {
            threads.emplace_back(thread_loop);
        }

        SP_LOG_INFO("%u threads have been created", thread_count);
    }

    void ThreadPool::Shutdown()
    {
        if (threads.empty())
        {
            return;
        }

        Flush(true);

        {
            lock_guard<mutex> lock(task_mutex);
            stopping = true;
            stopping_flag.store(true, memory_order_relaxed);
        }
        task_cv.notify_all();

        for (thread& t : threads)
        {
            t.join();
        }

        threads.clear();
        thread_count = 0;
    }

    future<void> ThreadPool::AddTask(Task&& task)
    {
        // a promise instead of packaged_task, so the work and its captures are destroyed before the future becomes ready
        struct AsyncTask
        {
            Task work;
            promise<void> done;
        };
        const shared_ptr<AsyncTask> job = make_shared<AsyncTask>();
        job->work                       = std::move(task);
        future<void> result             = job->done.get_future();

        {
            lock_guard<mutex> lock(task_mutex);
            if (stopping)
            {
                SP_LOG_WARNING("ThreadPool::AddTask() called while pool is stopping");
                return result;
            }

            push_locked([job]()
            {
                exception_ptr failure;
                try
                {
                    job->work();
                }
                catch (...)
                {
                    failure = current_exception();
                }
                job->work = nullptr;

                if (failure)
                {
                    job->done.set_exception(failure);
                }
                else
                {
                    job->done.set_value();
                }
            });
        }

        task_cv.notify_one();
        return result;
    }

    void ThreadPool::ParallelLoop(function<void(uint32_t, uint32_t)>&& work_fn, const uint32_t work_total)
    {
        SP_ASSERT_MSG(work_total > 0, "parallel loop requires work_total > 0");

        // the caller is one of the workers, so one pool thread stays free for async tasks
        const uint32_t workers = min(thread_count, work_total);
        depth_scope caller_depth;
        if (workers <= 1 || parallel_depth > 1)
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
        // the caller waits for every claimed chunk, so fn's captures outlive every call
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

                if (loop.done_chunk.fetch_add(1, memory_order_acq_rel) + 1 == loop.chunk_count)
                {
                    loop.done_chunk.notify_all();
                }
            }
        };

        const uint32_t helpers = workers - 1;
        bool queued            = false;
        {
            lock_guard<mutex> lock(task_mutex);
            if (!stopping)
            {
                for (uint32_t i = 0; i < helpers; ++i)
                {
                    push_locked([state, run_chunks]()
                    {
                        depth_scope chunk_depth;
                        run_chunks(*state);
                    });
                }
                queued = true;
            }
        }
        if (queued)
        {
            if (helpers + 1 >= thread_count)
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
        }

        run_chunks(*state);

        // the remaining chunks are already running on other threads, spin briefly then sleep until the last one lands
        for (uint32_t spin = 0; spin < 2048 && state->done_chunk.load(memory_order_acquire) < state->chunk_count; ++spin)
        {
            _mm_pause();
        }
        for (uint32_t done = state->done_chunk.load(memory_order_acquire); done < state->chunk_count; done = state->done_chunk.load(memory_order_acquire))
        {
            state->done_chunk.wait(done, memory_order_acquire);
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
        // the calling task is pending itself, waiting would never return
        if (is_worker_thread)
        {
            SP_LOG_ERROR("ThreadPool::Flush() called from a pool task, ignoring to avoid a deadlock");
            return;
        }

        if (remove_queued)
        {
            deque<Task> dropped;
            {
                lock_guard<mutex> lock(task_mutex);
                dropped.swap(tasks);
                queued_count.store(0, memory_order_relaxed);
            }

            // destroyed outside the lock, captures may break promises or schedule work from their destructors
            const uint32_t removed = static_cast<uint32_t>(dropped.size());
            dropped.clear();
            if (removed > 0)
            {
                release_pending(removed);
            }
        }

        unique_lock<mutex> lock(task_mutex);
        idle_cv.wait(lock, [] { return pending_count.load(memory_order_acquire) == 0; });
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
        const uint32_t working = working_count.load(memory_order_relaxed);
        return (thread_count > working) ? (thread_count - working) : 0;
    }

    bool ThreadPool::AreTasksRunning()
    {
        return pending_count.load(memory_order_relaxed) > 0;
    }
}
