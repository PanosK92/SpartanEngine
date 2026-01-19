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

    enum Tasking_Type
    {
        STATIC,
        PARALLEL,
    };

    enum class Task_Priority
    {   
        PRIORITY_LOW,
        PRIORITY_NORMAL,
        PRIORITY_MEDIUM,
        PRIORITY_HIGH
    };

    class Task_Manager
    {
    public:
        Task_Manager();
        ~Task_Manager();

        /**
         * @brief Executes the Taskflow using the Executor.
         */
        void Execute();

        /**
         * @brief Adds a task to the given Taskflow with an optional name.
         *
         * @tparam T The type of the callable to be added as a task.
         * @param taskflow The Taskflow instance to which the task will be added.
         * @param callable The callable object to be added as a task.
         * @param name An optional name for the task.
         * @return The created Task object.
         */
        template <typename T>
        Task AddTask(Taskflow& taskflow, T&& callable, std::string_view name = {})
        {
            Task task = taskflow.emplace(std::forward<T>(callable));
            if (!name.empty())
            {
                // Task::name accepts a std::string (or const std::string&).
                // Create a temporary std::string so Task stores its own copy.
                task.name(std::string(name));
            }
            return task;
        }
        
        /**
         * @brief Debug utility to dump the Taskflow task graph to a DOT file
         *
         * @param taskflow The Taskflow instance whose graph is to be dumped
         * @param filename The name of the DOT file to which the graph will be dumped
         * Paste the dot file to visualize the graph at https://dreampuf.github.io/GraphvizOnline
         */
        static void DumpGraph(const Taskflow& taskflow, const char* filename);

    private:
        Executor m_Executor;  // The Executor to run the Taskflow
        Taskflow m_Taskflow;  // The Taskflow instance

    };

}
