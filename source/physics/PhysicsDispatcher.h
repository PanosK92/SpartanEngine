/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <physx/task/PxCpuDispatcher.h>
#include <physx/task/PxTask.h>
#include <physx/foundation/PxFPU.h>
#include <physx/common/PxProfileZone.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace spartan
{
    // The simulation caller can execute ready PhysX tasks while waiting for its
    // results. Workers sleep between steps instead of spinning during rendering.
    class PhysicsDispatcher final : public physx::PxCpuDispatcher
    {
    public:
        explicit PhysicsDispatcher(uint32_t count)
        {
            workers.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
                workers.emplace_back([this]
                {
                    for (;;)
                    {
                        physx::PxBaseTask* task;
                        {
                            std::unique_lock lock(mutex);
                            available.wait(lock, [this] { return stopping || !tasks.empty(); });
                            if (tasks.empty() && stopping) return;
                            task = tasks.front();
                            tasks.pop_front();
                        }
                        Run(task);
                    }
                });
        }

        ~PhysicsDispatcher() override
        {
            // Scene destruction precedes dispatcher destruction; no producers remain.
            {
                std::lock_guard lock(mutex);
                stopping = true;
            }
            available.notify_all();
            for (auto& worker : workers) worker.join();
        }

        void release() { delete this; }
        uint32_t getWorkerCount() const override { return static_cast<uint32_t>(workers.size()); }

        void submitTask(physx::PxBaseTask& task) override
        {
            {
                std::lock_guard lock(mutex);
                if (task.isHighPriority()) tasks.push_front(&task);
                else tasks.push_back(&task);
            }
            available.notify_one();
        }

        bool Assist()
        {
            physx::PxBaseTask* task;
            {
                std::lock_guard lock(mutex);
                if (tasks.empty()) return false;
                task = tasks.front();
                tasks.pop_front();
            }
            Run(task);
            return true;
        }

    private:
        static void Run(physx::PxBaseTask* task)
        {
            // Worker threads and the assisting main thread need the same PhysX
            // floating-point mode; restore the caller's mode after the task.
            physx::PxFPUGuard floating_point_guard;
            {
                physx::PxProfileScoped profile(PxGetProfilerCallback(), task->getName(), false, task->getContextId());
                task->run();
            }
            task->release(); // May schedule dependent tasks; never hold the queue lock here.
        }

        std::mutex mutex;
        std::condition_variable available;
        std::deque<physx::PxBaseTask*> tasks;
        std::vector<std::thread> workers;
        bool stopping = false;
    };
}
