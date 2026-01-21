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
#include "Frame_Tasking.h"
#include "Taskflow_Executor.h"
//=============================

//= NAMESPACES =====
using namespace tf;
//==================

namespace spartan
{
    
    FrameTaskflow::FrameTaskflow() : m_needs_rebuild(true)
    {
        // Default empty callbacks
        m_input_callback = [](){};
        m_ai_callback = [](){};
        m_physics_callback = [](){};
        m_renderer_callback = [](){};
    }
    
    void FrameTaskflow::SetInputCallback(FrameCallback callback)
    {
        m_input_callback = callback ? callback : [](){};
        m_needs_rebuild = true;
    }
    
    void FrameTaskflow::SetAICallback(FrameCallback callback)
    {
        m_ai_callback = callback ? callback : [](){};
        m_needs_rebuild = true;
    }
    
    void FrameTaskflow::SetPhysicsCallback(FrameCallback callback)
    {
        m_physics_callback = callback ? callback : [](){};
        m_needs_rebuild = true;
    }
    
    void FrameTaskflow::SetRendererCallback(FrameCallback callback)
    {
        m_renderer_callback = callback ? callback : [](){};
        m_needs_rebuild = true;
    }
    
    void FrameTaskflow::Execute()
    {
        RebuildTaskflow();
    
        auto& executor = TaskflowExecutor::GetInstance();
        executor.run(m_taskflow).wait();
    }
    
    tf::Future<void> FrameTaskflow::ExecuteAsync()
    {
        RebuildTaskflow();
    
        auto& executor = TaskflowExecutor::GetInstance();
        return executor.run(m_taskflow);
    }
    
    tf::Taskflow& FrameTaskflow::GetTaskflow()
    {
        RebuildTaskflow();
        return m_taskflow;
    }
    
    void FrameTaskflow::RebuildTaskflow()
    {
        if (!m_needs_rebuild) { return; }
    
        // Clear existing taskflow
        m_taskflow.clear();
    
        // Create tasks for each stage
        auto inputTask = m_taskflow.emplace([this]() { m_input_callback(); }).name("Input Processing");
    
        auto aiTask = m_taskflow.emplace([this]() { m_ai_callback(); }).name("AI Update");
    
        auto physicsTask = m_taskflow.emplace([this]() { m_physics_callback(); }).name("Physics Simulation");
    
        auto rendererTask = m_taskflow.emplace([this]() { m_renderer_callback(); }).name("Renderer Submission");
    
        // Set up dependencies: input -> AI -> physics -> renderer
        inputTask.precede(aiTask);
        aiTask.precede(physicsTask);
        physicsTask.precede(rendererTask);
    
        m_needs_rebuild = false;
    }
    
}
