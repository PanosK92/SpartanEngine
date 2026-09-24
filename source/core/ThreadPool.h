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

        // spread execution of a given function across all available threads
        static void ParallelLoop(std::function<void(uint32_t work_index_start, uint32_t work_index_end)>&& function, const uint32_t work_total);

        // wait for all threads to finish work
        static void Flush(bool remove_queued = false);

        // stats
        static uint32_t GetThreadCount();
        static uint32_t GetWorkingThreadCount();
        static uint32_t GetIdleThreadCount();
        static bool AreTasksRunning();
    };
}
