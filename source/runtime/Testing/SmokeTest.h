/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include "../Core/Event.h"
#include <string>
#include <vector>
#include <chrono>
#include <memory>
//=============================

namespace spartan
{
    class Entity;
    class RHI_Buffer;
    class RHI_Texture;
    class SmokeTest
    {
    public:
        static void Initialize();
        static void Shutdown();
        static void Tick();

    private:
        static void OnFirstFrameCompleted();
        static void RunInitialTests();
        static void RunDelayedTests();
        static void RunTest(const char* name, bool (*test_func)(std::string&));

        static bool m_delayedTestsPending;
        static uint32_t m_testCount;
        static uint32_t m_passedCount;
        static std::string m_error;
        static bool m_testsPassed;
        static double m_startTimeMs;

        // Individual Tests
        static bool Test_RHI_BackendInitialization(std::string& out_error);
        static bool Test_RHI_MemoryAllocation(std::string& out_error);
        static bool Test_Shader_CompilationPipeline(std::string& out_error);
        static bool Test_Renderer_PipelineStates(std::string& out_error);
        static bool Test_RHI_CommandListRecording(std::string& out_error);
        static bool Test_RHI_ResourceTransitions(std::string& out_error);
        static bool Test_Threading_ResourceCreation(std::string& out_error);
        static bool Test_Render_BasicCube(std::string& out_error);

    private:
        static Entity* CreateTestCamera(const char* name, const math::Vector3& position);
        static Entity* CreateTestLight(const char* name, const math::Vector3& position, float intensity);
        static Entity* CreateTestCube(const char* name, const math::Vector3& position);
        static std::unique_ptr<RHI_Buffer> CreateStagingBuffer(RHI_Texture* texture, std::string& out_error);
        static bool CopyTextureToBuffer(RHI_Texture* texture, RHI_Buffer* buffer, std::string& out_error);
        static bool ValidateCenterPixel(void* data, uint32_t width, uint32_t height, uint32_t bits_per_channel, uint32_t channel_count);
    };
}
