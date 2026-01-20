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

    //==============================================================================//
    //==============================================================================//
    //                                                                              //
    //  NOTE: THIS IS PURELY EXPERIMENTAL AT THIS TIME! DO NOT USE IN PRODUCTION.   //
    //                                                                              //
    //==============================================================================//
    //==============================================================================//

    /**
     * @brief Example frame taskflow for typical game loop stages.
     *
     * Demonstrates a common pattern in game engines where frame processing
     * is split into stages with clear dependencies:
     *
     * Input Processing → AI Update → Physics Simulation → Renderer Submission
     *
     * Each stage can internally parallelize work while maintaining
     * deterministic ordering between stages.
     *
     * @note This is a reference implementation. Real subsystems should
     *       create their own taskflows based on their specific needs.
     *
     * @example
     * @code
     * RailNex::FrameTaskflow frameFlow;
     * frameFlow.SetInputCallback([](){ processInput(); });
     * frameFlow.SetAICallback([](){ updateAI(); });
     * frameFlow.SetPhysicsCallback([](){ simulatePhysics(); });
     * frameFlow.SetRendererCallback([](){ submitRenderCommands(); });
     *
     * // Execute one frame
     * frameFlow.Execute();
     * @endcode
     */
    class FrameTaskflow
    {
    public:
        using FrameCallback = std::function<void()>;
    
        FrameTaskflow();
        ~FrameTaskflow() = default;
    
        /**
         * @brief Set the input processing callback.
         *
         * This callback is executed first in the frame pipeline.
         * Typically handles input events, device polling, etc.
         *
         * @param callback Function to execute during input stage.
         */
        void SetInputCallback(FrameCallback callback);
    
        /**
         * @brief Set the AI update callback.
         *
         * This callback is executed after input processing.
         * Typically handles AI decision-making, pathfinding, etc.
         *
         * @param callback Function to execute during AI stage.
         */
        void SetAICallback(FrameCallback callback);
    
        /**
         * @brief Set the physics simulation callback.
         *
         * This callback is executed after AI update.
         * Typically handles physics simulation, collision detection, etc.
         *
         * @param callback Function to execute during physics stage.
         */
        void SetPhysicsCallback(FrameCallback callback);
    
        /**
         * @brief Set the renderer submission callback.
    
         * This callback is executed last in the frame pipeline.
         * Typically handles render command submission, scene culling, etc.
         *
         * @param callback Function to execute during renderer stage.
         */
        void SetRendererCallback(FrameCallback callback);
    
        /**
         * @brief Execute one frame of the pipeline.
         *
         * Runs all configured callbacks in sequence with proper dependencies.
         * Blocks until all stages complete.
         */
        void Execute();
    
        /**
         * @brief Execute one frame asynchronously.
         * Schedules the frame pipeline for execution and returns immediately.
         * @return Future that can be used to wait for frame completion.
         */
        tf::Future<void> ExecuteAsync();
    
        /**
         * @brief Get the underlying taskflow for customization.
         * Allows users to modify the frame pipeline structure. 
         * Might not be safe for multiple Vulkan PSO's.
         * @return Reference to the internal taskflow.
         */
        tf::Taskflow& GetTaskflow();
    
    private:
        void RebuildTaskflow();
    
        tf::Taskflow m_taskflow;
        FrameCallback m_input_callback;
        FrameCallback m_ai_callback;
        FrameCallback m_physics_callback;
        FrameCallback m_renderer_callback;
        bool m_needs_rebuild = false;
    };

}
