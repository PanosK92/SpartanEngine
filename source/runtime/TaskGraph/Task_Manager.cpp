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
#include "Task_Manager.h"

#include "FileSystem/FileSystem.h"

//=============================

//= NAMESPACES =====
using namespace std;
using namespace tf;
//==================

namespace spartan
{
/**
 * @brief Converts a Task_Priority enum to an integer value
 * @param priority The priority level of the task
 * @return An integer representing the priority level
 */
    static int PriorityLevelToInt(Task_Priority priority)
    {
        switch (priority)
        {
            case Task_Priority::PRIORITY_LOW:     return 0;
            case Task_Priority::PRIORITY_NORMAL:  return 1;
            case Task_Priority::PRIORITY_MEDIUM:  return 2;
            case Task_Priority::PRIORITY_HIGH:    return 3;
            default:                              return 1;
        }
    }

    Task_Manager::Task_Manager()
    {
        
    }

    Task_Manager::~Task_Manager()
    {
        
    }

    void Task_Manager::Execute()
    {
        
    }

    void Task_Manager::DumpGraph(const Taskflow& taskflow, const char* filename)
    { 
        taskflow.dump(std::cout);
        //FileSystem::WriteTextFile(filename);
    }

}  // namespace spartan
