/*
Copyright(c) 2015-2026 Panos Karabelas & Thomas Ray

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
#pragma once

//= INCLUDES ==================

#include <taskflow/taskflow.hpp>
//=============================

//= NAMESPACES =====
using namespace tf;
//==================

namespace spartan
{
    /**
     * @brief Engine-level Taskflow executor singleton.
     *
     * Provides a long-lived, thread-safe executor for scheduling
     * parallel tasks across Spartan subsystems. The executor is
     * initialized on first access and should be explicitly shut down
     * during engine cleanup.
     *
     * @note This executor uses a thread pool sized to the hardware
     *       concurrency available on the system.
     *
     * @example
     * @code
     * auto& executor = spartan::TaskflowExecutor::GetInstance();
     * tf::Taskflow taskflow;
     * taskflow.emplace([](){ std::cout << "Hello from task!\n"; });
     * executor.run(taskflow).wait();
     * @endcode
     */
    class TaskflowExecutor
    {
    public:
        /**
         * @brief Get the singleton instance of the TaskflowExecutor.
         *
         * Thread-safe lazy initialization. The executor is created on
         * first access with a thread pool sized to hardware concurrency.
         *
         * @return Reference to the singleton tf::Executor instance.
         */
        static Executor& GetInstance();
    
        /**
         * @brief Shutdown the TaskflowExecutor and release resources.
         *
         * Waits for all pending tasks to complete before destroying
         * the executor. Should be called during engine shutdown.
         *
         * @warning After calling Shutdown(), calling GetInstance() will
         *          create a new executor instance.
         */
        static void Shutdown();
    
        /**
         * @brief Check if the executor has been initialized.
         * @return true if executor exists, false otherwise.
         */
        static bool IsInitialized();
    
        /**
         * @brief Get the number of worker threads in the executor.
         * @return Number of worker threads, or 0 if not initialized.
         */
        static std::size_t GetWorkerCount();

    private:
        TaskflowExecutor()                                   = delete;
        ~TaskflowExecutor()                                  = delete;
        TaskflowExecutor(const TaskflowExecutor&)            = delete;
        TaskflowExecutor& operator=(const TaskflowExecutor&) = delete;
    
        static std::unique_ptr<Executor> s_task_executor;
        static std::mutex s_task_mutex;
    };
    
}  // namespace spartan
