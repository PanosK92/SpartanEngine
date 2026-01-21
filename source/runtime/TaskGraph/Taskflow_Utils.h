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
#include "Taskflow_Executor.h"
#include <taskflow/taskflow.hpp>
//=============================

//= NAMESPACES =====
using namespace tf;
//==================

namespace spartan
{
    namespace utils
    {
        /**
         * @brief Run a taskflow once and wait for completion.
         *
         * Convenience wrapper for simple fire-and-wait taskflow execution.
         *
         * @param taskflow The taskflow to execute.
         * @return Reference to the future for the taskflow execution.
         *
         * @example
         * @code
         * tf::Taskflow tf;
         * tf.emplace([](){ processData(); });
         * spartan::utils::RunOnce(tf);
         * @endcode
         */
        inline tf::Future<void> RunOnce(tf::Taskflow& taskflow)
        {
            auto& executor = TaskflowExecutor::GetInstance();
            return executor.run(taskflow);
        }

        
        /**
         * @brief Run a taskflow asynchronously without waiting.
         *
         * Schedules the taskflow for execution and returns immediately.
         * Caller can wait on the returned future if needed.
         *
         * @param taskflow The taskflow to execute.
         * @return Future that can be used to wait for completion.
         */
        inline tf::Future<void> RunAsync(tf::Taskflow& taskflow)
        {
            auto& executor = TaskflowExecutor::GetInstance();
            return executor.run(taskflow);
        }

        /**
         * @brief Wait for all pending tasks in the executor to complete.
         * Blocks until all currently scheduled tasks have finished execution.
         */
        inline void WaitForAll()
        {
            auto& executor = TaskflowExecutor::GetInstance();
            executor.wait_for_all();
        }

        /**
         * @brief Create a parallel_for task using default parameters.
         * Helper to simplify parallel iteration over a range.
         *
         * @tparam Iterator Iterator type.
         * @tparam Callable Function type.
         * @param taskflow The taskflow to add the task to.
         * @param first Beginning iterator.
         * @param last End iterator.
         * @param callable Function to call for each element.
         * @return Task handle for the parallel_for operation.
         *
         * @example
         * @code
         * std::vector<int> data(1000);
         * tf::Taskflow tf;
         * RailNex::TaskflowUtils::ParallelFor(tf, data.begin(), data.end(),
         *     [](int& value){ value *= 2; });
         * @endcode
         */
        template <typename Iterator, typename Callable>
        inline tf::Task ParallelFor(tf::Taskflow& taskflow, Iterator first, Iterator last, Callable&& callable)
        {
            return taskflow.for_each(first, last, std::forward<Callable>(callable));
        }

        /**
         * @brief Create a parallel_for task with index-based iteration.
         * Helper for parallel iteration over an index range.
         *
         * @tparam IndexType Integer type for indices.
         * @tparam Callable Function type.
         * @param taskflow The taskflow to add the task to.
         * @param first Beginning index.
         * @param last End index (exclusive).
         * @param step Step size.
         * @param callable Function to call for each index.
         * @return Task handle for the parallel_for operation.
         */
        template <typename IndexType, typename Callable>
        inline tf::Task ParallelForIndex(
            tf::Taskflow& taskflow, IndexType first, IndexType last, IndexType step, Callable&& callable)
        {
            return taskflow.for_each_index(first, last, step, std::forward<Callable>(callable));
        }

        /**
         * @brief Create a subflow task for dynamic task creation.
         * Convenience wrapper for creating tasks that spawn dynamic subtasks.
         *
         * @tparam Callable Function type that accepts a tf::Subflow&.
         * @param taskflow The taskflow to add the task to.
         * @param callable Function that creates subtasks.
         * @return Task handle for the subflow operation.
         *
         * @example
         * @code
         * tf::Taskflow tf;
         * RailNex::TaskflowUtils::CreateSubflow(tf, [](tf::Subflow& sf){
         *     sf.emplace([](){ task1(); });
         *     sf.emplace([](){ task2(); });
         * });
         * @endcode
         */
        template <typename Callable>
        inline tf::Task CreateSubflow(tf::Taskflow& taskflow, Callable&& callable)
        {
            return taskflow.emplace([callable = std::forward<Callable>(callable)](tf::Subflow& sf) mutable { callable(sf); });
        }

        /**
         * @brief Run a single async task and return a std::future.
         * Simplified wrapper for running a single function asynchronously
         * using Taskflow's async API.
         *
         * @tparam Callable Function type.
         * @tparam Args Argument types.
         * @param callable Function to execute.
         * @param args Arguments to pass to the function.
         * @return std::future for the result.
         *
         * @note Uses tf::Executor::async internally.
         */
        template <typename Callable, typename... Args>
        inline auto Async(Callable&& callable, Args&&... args)
        {
            auto& executor = TaskflowExecutor::GetInstance();
            return executor.async(std::forward<Callable>(callable), std::forward<Args>(args)...);
        }

        /**
         * @brief Execute a simple parallel reduction operation.
         * Helper for common reduction patterns (sum, max, min, etc.).
         *
         * @tparam Iterator Iterator type.
         * @tparam T Result type.
         * @tparam BinaryOp Binary operation type.
         * @param taskflow The taskflow to add the task to.
         * @param first Beginning iterator.
         * @param last End iterator.
         * @param init Initial value.
         * @param bop Binary operation for reduction.
         * @return Task handle for the reduction operation.
         */
        template <typename Iterator, typename T, typename BinaryOp>
        inline tf::Task Reduce(tf::Taskflow& taskflow, Iterator first, Iterator last, T& init, BinaryOp&& bop)
        {
            return taskflow.reduce(first, last, init, std::forward<BinaryOp>(bop));
        }

    }

}
