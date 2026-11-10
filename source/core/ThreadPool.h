/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ========
#include <future>
#include <functional>
//===================

namespace spartan
{
    using Task = std::function<void()>;

    class ThreadPool
    {
    public:
        static void Initialize();
        static void Shutdown();

        // add a task
        static std::future<void> AddTask(Task&& task);

        // runs a function on a dedicated thread, the browser has no threads so it runs inline and the future is ready on return
        template<typename Function>
        static auto RunDetached(Function&& function) -> std::future<std::invoke_result_t<std::decay_t<Function>>>
        {
#if defined(__EMSCRIPTEN__)
            std::packaged_task<std::invoke_result_t<std::decay_t<Function>>()> task(std::forward<Function>(function));
            auto result = task.get_future();
            task();
            return result;
#else
            return std::async(std::launch::async, std::forward<Function>(function));
#endif
        }

        // spread execution of a given function across all available threads
        static void ParallelLoop(std::function<void(uint32_t work_index_start, uint32_t work_index_end)>&& function, const uint32_t work_total);

        // memcpy that splits multi-megabyte copies across the pool, inline on pool workers
        static void ParallelCopy(void* destination, const void* source, size_t size);

        // wait for all queued and running tasks to finish, not callable from a pool task
        static void Flush(bool remove_queued = false);

        // stats
        static uint32_t GetThreadCount();
        static uint32_t GetWorkingThreadCount();
        static uint32_t GetIdleThreadCount();
        static bool AreTasksRunning();
    };
}
