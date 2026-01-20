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

//= INCLUDES ==================
#include "pch.h"
#include "Taskflow_Executor.h"
//=============================

//= NAMESPACES =====
using namespace tf;
//==================

namespace spartan
{
std::unique_ptr<tf::Executor> TaskflowExecutor::s_task_executor = nullptr;
std::mutex TaskflowExecutor::s_task_mutex;

    tf::Executor& TaskflowExecutor::GetInstance()
    {
        // Use double-checked locking for thread-safe lazy initialization
        if (!s_task_executor)
        {
            std::scoped_lock lock(s_task_mutex);
            if (!s_task_executor)
            {
                // TODO:: Make the number of threads configurable

                // Use hardware concurrency for thread pool size
                unsigned numThreads = std::thread::hardware_concurrency();
                if (numThreads == 0)
                {
                    numThreads = 4; // Fallback to 4 threads if detection fails
                }  
    
                s_task_executor = std::make_unique<tf::Executor>(numThreads);
            }
        }

        return *s_task_executor;
    }

    void TaskflowExecutor::Shutdown()
    {
        std::scoped_lock lock(s_task_mutex);
        if (s_task_executor)
        {
            // Wait for all tasks to complete
            s_task_executor->wait_for_all();
            s_task_executor.reset();
        }
    }
    
    bool TaskflowExecutor::IsInitialized()
    {
        std::scoped_lock lock(s_task_mutex);
        return s_task_executor != nullptr;
    }
    
    std::size_t TaskflowExecutor::GetWorkerCount()
    {
        std::scoped_lock lock(s_task_mutex);
        if (s_task_executor)
        {
            return s_task_executor->num_workers();
        }
    
        return 0;
    }
    
}  // namespace spartan
