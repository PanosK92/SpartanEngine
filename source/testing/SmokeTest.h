/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==================
#include "../core/Event.h"
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

        static bool m_delayed_tests_pending;
        static uint32_t m_test_count;
        static uint32_t m_passed_count;
        static std::string m_error;
        static bool m_all_tests_passed;
        static double m_start_time_ms;

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
