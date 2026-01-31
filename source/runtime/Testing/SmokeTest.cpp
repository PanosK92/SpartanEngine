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

//= INCLUDES =====================
#include "pch.h"
#include "SmokeTest.h"
#include "../Core/Engine.h"
#include "../Core/Timer.h"
#include "../Logging/Log.h"
#include "../Rendering/Renderer.h"
#include "../Rendering/Material.h"
#include "../Resource/Import/ImageImporter.h"
#include "../Resource/IResource.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_InputLayout.h"
#include "../RHI/RHI_Texture.h"
#include "../RHI/RHI_Buffer.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Device.h"
#include "../World/World.h"
#include "../World/Entity.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Components/Renderable.h"
#include "../World/Components/Physics.h"
#include "../FileSystem/FileSystem.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
//================================

namespace spartan
{
    bool SmokeTest::m_delayedTestsPending = false;
    uint32_t SmokeTest::m_testCount = 0;
    uint32_t SmokeTest::m_passedCount = 0;
    std::string SmokeTest::m_error;
    bool SmokeTest::m_testsPassed = true;
    double SmokeTest::m_startTimeMs = 0.0;

    namespace
    {
        std::chrono::steady_clock::time_point start_time;
    }

    void SmokeTest::Initialize()
    {
        SP_SUBSCRIBE_TO_EVENT(EventType::RendererOnFirstFrameCompleted, SP_EVENT_HANDLER_STATIC(OnFirstFrameCompleted));
    }

    void SmokeTest::Shutdown()
    {

    }

    void SmokeTest::OnFirstFrameCompleted()
    {
        if (Engine::HasArgument("-ci_test"))
            RunInitialTests();
    }

    void SmokeTest::Tick()
    {
        if (!m_delayedTestsPending)
            return;

        Material* standard_material = Renderer::GetStandardMaterial().get();
        if (standard_material && standard_material->GetResourceState() >= ResourceState::PreparedForGpu)
        {
            m_delayedTestsPending = false;
            RunDelayedTests();
        }
    }

    void SmokeTest::RunTest(const char* name, bool (*test_func)(std::string&))
    {
        m_testCount++;
        m_error.clear();
        SP_LOG_INFO("Running: %s...", name);

        if (test_func(m_error))
        {
            m_passedCount++;
            SP_LOG_INFO("  ✓ PASSED: %s", name);
        }
        else
        {
            m_testsPassed = false;
            SP_LOG_ERROR("  ✗ FAILED: %s - %s", name, m_error.c_str());
        }
    }

    void SmokeTest::RunInitialTests()
    {
        SP_LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        SP_LOG_INFO("Starting Smoke Tests...");
        SP_LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        m_startTimeMs = Timer::GetTimeMs();
        m_testCount = 0;
        m_passedCount = 0;
        m_error.clear();
        m_testsPassed = true;

        RunTest("RHI.BackendInitialization",  Test_RHI_BackendInitialization);
        RunTest("RHI.MemoryAllocation",          Test_RHI_MemoryAllocation);
        RunTest("Shader.CompilationPipeline",   Test_Shader_CompilationPipeline);
        RunTest("Renderer.PipelineStates",       Test_Renderer_PipelineStates);
        RunTest("RHI.CommandListRecording",   Test_RHI_CommandListRecording);
        RunTest("RHI.ResourceTransitions",      Test_RHI_ResourceTransitions);
        RunTest("Threading.ResourceCreation",  Test_Threading_ResourceCreation);

        m_delayedTestsPending = true;
    }

    void SmokeTest::RunDelayedTests()
    {
        RunTest("Render.BasicCube", Test_Render_BasicCube);

        double elapsed_ms = Timer::GetTimeMs() - m_startTimeMs;
        std::ofstream file("ci_test.txt");
        if (file.is_open())
        {
            if (m_testsPassed)
            {
                file << "0";
                SP_LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                SP_LOG_INFO("Smoke Tests: %d/%d PASSED in %.2f ms", m_passedCount, m_testCount, elapsed_ms);
                SP_LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            }
            else
            {
                file << "1" << std::endl;
                file << m_error;
                SP_LOG_ERROR("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                SP_LOG_ERROR("Smoke Tests: %d/%d FAILED in %.2f ms", m_passedCount, m_testCount, elapsed_ms);
                SP_LOG_ERROR("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            }
            file.close();
        }
    }

    bool SmokeTest::Test_RHI_BackendInitialization(std::string& out_error)
    {
        if (!RHI_Device::GetPrimaryPhysicalDevice())
        {
            out_error = "No physical device detected";
            return false;
        }

        if (RHI_Device::MemoryGetAllocatedMb() == 0 && RHI_Device::MemoryGetAvailableMb() > 0)
        {
            out_error = "Device reports zero allocated memory after init";
            return false;
        }

        if (!RHI_Device::GetQueue(RHI_Queue_Type::Graphics))
        {
            out_error = "Graphics queue not initialized";
            return false;
        }

        if (!Engine::HasArgument("-headless"))
        {
            RHI_SwapChain* swap_chain = Renderer::GetSwapChain();
            if (!swap_chain)
            {
                out_error = "Swap chain not initialized";
                return false;
            }
        }

        return true;
    }

    bool SmokeTest::Test_RHI_MemoryAllocation(std::string& out_error)
    {
        const uint32_t vertex_count = 3;
        const uint32_t vertex_size = sizeof(float) * 3;
        
        auto vertex_buffer = std::make_unique<RHI_Buffer>(
            RHI_Buffer_Type::Vertex,
            vertex_size,
            vertex_count,
            nullptr,
            false,
            "smoke_test_vb"
        );

        if (!vertex_buffer->GetRhiResource())
        {
            out_error = "Failed to allocate vertex buffer";
            return false;
        }

        // Check memory tracking
        uint64_t allocated_mb = RHI_Device::MemoryGetAllocatedMb();
        if (allocated_mb == 0)
        {
            out_error = "Memory tracking not functioning (reports 0 MB)";
            return false;
        }

        return true;
    }

    bool SmokeTest::Test_Shader_CompilationPipeline(std::string& out_error)
    {
        const std::string minimal_vs = R"(
            struct VS_INPUT { float3 pos : POSITION; };
            struct VS_OUTPUT { float4 pos : SV_POSITION; };

            VS_OUTPUT main_vs(VS_INPUT input) {
                VS_OUTPUT output;
                output.pos = float4(input.pos, 1.0);
                return output;
            }
        )";

        std::string test_vs_path = "smoke_test_minimal.vs.hlsl";
        
        {
            std::ofstream shader_file(test_vs_path);
            if (!shader_file.is_open())
            {
                out_error = "Failed to create temporary shader file";
                return false;
            }
            shader_file << minimal_vs;
            shader_file.close();
        }

        auto shader = std::make_unique<RHI_Shader>();
        shader->Compile(RHI_Shader_Type::Vertex, test_vs_path, false);
        
        // Cleanup file
        FileSystem::Delete(test_vs_path);

        if (!shader->IsCompiled())
        {
            out_error = "Shader compilation failed";
            return false;
        }

        return true;
    }

    bool SmokeTest::Test_RHI_CommandListRecording(std::string& out_error)
    {
        auto texture = std::make_unique<RHI_Texture>(
            RHI_Texture_Type::Type2D,
            64, 64, 1, 1,
            RHI_Format::R8G8B8A8_Unorm,
            RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit,
            "smoke_test_cmd_texture"
        );

        if (RHI_CommandList* cmd = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics))
        {
            cmd->InsertBarrier(
                texture->GetRhiResource(),
                texture->GetFormat(),
                0, 1, 1,
                RHI_Image_Layout::Attachment
            );

            // Clear
            cmd->ClearTexture(texture.get(), Color(1, 0, 0, 1));

            cmd->InsertBarrier(
                texture->GetRhiResource(),
                texture->GetFormat(),
                0, 1, 1,
                RHI_Image_Layout::Shader_Read
            );

            RHI_CommandList::ImmediateExecutionEnd(cmd);
        }
        else
        {
            out_error = "Failed to begin immediate command list";
            return false;
        }

        return true;
    }

    bool SmokeTest::Test_RHI_ResourceTransitions(std::string& out_error)
    {
        auto texture = std::make_unique<RHI_Texture>(
            RHI_Texture_Type::Type2D,
            64, 64, 1, 1,
            RHI_Format::R8G8B8A8_Unorm,
            RHI_Texture_Srv | RHI_Texture_Uav,
            "smoke_test_barrier"
        );

        if (RHI_CommandList* cmd = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics))
        {
            cmd->InsertBarrier(
                texture->GetRhiResource(),
                texture->GetFormat(),
                0, 1, 1,
                RHI_Image_Layout::General
            );

            RHI_CommandList::ImmediateExecutionEnd(cmd);
        }
        else
        {
            out_error = "Failed to begin command list";
            return false;
        }

        return true;
    }

    bool SmokeTest::Test_Threading_ResourceCreation(std::string& out_error)
    {
        const uint32_t thread_count = 4;
        const uint32_t resources_per_thread = 10;
        
        std::vector<std::thread> threads;
        std::atomic<uint32_t> success_count{0};
        std::atomic<uint32_t> failure_count{0};
        
        for (uint32_t t = 0; t < thread_count; ++t)
        {
            threads.emplace_back([&, t]()
            {
                for (uint32_t i = 0; i < resources_per_thread; ++i)
                {
                    std::string name = "smoke_mt_buffer_" + std::to_string(t) + "_" + std::to_string(i);
                    
                    auto buffer = std::make_unique<RHI_Buffer>(
                        RHI_Buffer_Type::Vertex,
                        sizeof(float) * 3,
                        100,
                        nullptr,
                        false,
                        name.c_str()
                    );
                    
                    if (buffer && buffer->GetRhiResource())
                        success_count++;
                    else
                        failure_count++;
                }
            });
        }
        
        for (auto& thread : threads)
            thread.join();
        
        if (failure_count > 0)
        {
            out_error = "Multi-threaded resource creation failed: " + 
                        std::to_string(failure_count.load()) + " failures";
            return false;
        }
        
        return true;
    }

    bool SmokeTest::Test_Renderer_PipelineStates(std::string& out_error)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(Renderer_RasterizerState::Max); ++i)
        {
            if (!Renderer::GetRasterizerState(static_cast<Renderer_RasterizerState>(i)))
            {
                out_error = "Missing Rasterizer State at index " + std::to_string(i);
                return false;
            }
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(Renderer_DepthStencilState::Max); ++i)
        {
            if (!Renderer::GetDepthStencilState(static_cast<Renderer_DepthStencilState>(i)))
            {
                out_error = "Missing Depth Stencil State at index " + std::to_string(i);
                return false;
            }
        }

        for (uint32_t i = 0; i < 3; ++i)
        {
            if (!Renderer::GetBlendState(static_cast<Renderer_BlendState>(i)))
            {
                out_error = "Missing Blend State at index " + std::to_string(i);
                return false;
            }
        }

        return true;
    }

    Entity* SmokeTest::CreateTestCamera(const char* name, const math::Vector3& position)
    {
        Entity* entity = World::CreateEntity();
        entity->SetObjectName(name);
        entity->AddComponent<Camera>();
        entity->SetPositionLocal(position);
        return entity;
    }

    Entity* SmokeTest::CreateTestLight(const char* name, const math::Vector3& position, float intensity)
    {
        Entity* entity = World::CreateEntity();
        entity->SetObjectName(name);
        Light* light = entity->AddComponent<Light>();
        light->SetLightType(LightType::Directional);
        entity->SetPositionLocal(position);
        light->SetIntensity(intensity);
        return entity;
    }

    Entity* SmokeTest::CreateTestCube(const char* name, const math::Vector3& position)
    {
        Entity* entity = World::CreateEntity();
        entity->SetObjectName(name);
        Renderable* renderable = entity->AddComponent<Renderable>();
        renderable->SetMesh(MeshType::Cube);
        renderable->SetMaterial(Renderer::GetStandardMaterial());
        entity->SetPositionLocal(position);
        return entity;
    }

    std::unique_ptr<RHI_Buffer> SmokeTest::CreateStagingBuffer(RHI_Texture* texture, std::string& out_error)
    {
        if (!texture)
        {
            out_error = "Null texture provided";
            return nullptr;
        }

        uint32_t width = texture->GetWidth();
        uint32_t height = texture->GetHeight();
        uint32_t bits_per_channel = texture->GetBitsPerChannel();
        uint32_t channel_count = texture->GetChannelCount();
        size_t data_size = static_cast<size_t>(width) * height * (bits_per_channel / 8) * channel_count;

        std::unique_ptr<RHI_Buffer> staging = std::make_unique<RHI_Buffer>(
            RHI_Buffer_Type::Constant,
            data_size,
            1,
            nullptr,
            true,
            "screenshot_staging"
        );

        if (!staging)
        {
            out_error = "Failed to create staging buffer";
            return nullptr;
        }

        return staging;
    }

    bool SmokeTest::CopyTextureToBuffer(RHI_Texture* texture, RHI_Buffer* buffer, std::string& out_error)
    {
        if (!texture || !buffer)
        {
            out_error = "Null texture or buffer provided";
            return false;
        }

        if (RHI_CommandList* cmd_list = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics))
        {
            cmd_list->CopyTextureToBuffer(texture, buffer);
            RHI_CommandList::ImmediateExecutionEnd(cmd_list);
            return true;
        }

        out_error = "Failed to begin immediate command list";
        return false;
    }

    bool SmokeTest::ValidateCenterPixel(void* data, uint32_t width, uint32_t height, uint32_t bits_per_channel, uint32_t channel_count)
    {
        if (!data)
            return false;

        size_t center_pixel_index = (height / 2) * width + (width / 2);
        size_t pixel_size = (bits_per_channel / 8) * channel_count;
        uint8_t* pixel_start = static_cast<uint8_t*>(data) + (center_pixel_index * pixel_size);

        for (size_t i = 0; i < pixel_size; ++i)
        {
            if (pixel_start[i] != 0)
                return true;
        }

        return false;
    }

    bool SmokeTest::Test_Render_BasicCube(std::string& out_error)
    {
        ConsoleRegistry::Get().SetValueFromString("r.ray_traced_reflections", "0");

        Entity* entity_camera = CreateTestCamera("SmokeTest_Camera", math::Vector3(0.0f, 0.0f, -5.0f));
        Entity* entity_light = CreateTestLight("SmokeTest_Light", math::Vector3(0.0f, 10.0f, 0.0f), 120000.0f);
        Entity* entity_cube = CreateTestCube("SmokeTest_Cube", math::Vector3(0.0f, 0.0f, 0.0f));

        RHI_Texture* frame_output = Renderer::GetRenderTarget(Renderer_RenderTarget::frame_output);
        if (!frame_output)
        {
            out_error = "Failed to get frame output render target";
            World::RemoveEntity(entity_cube);
            World::RemoveEntity(entity_light);
            World::RemoveEntity(entity_camera);
            return false;
        }

        std::unique_ptr<RHI_Buffer> staging = CreateStagingBuffer(frame_output, out_error);
        if (!staging)
        {
            World::RemoveEntity(entity_cube);
            World::RemoveEntity(entity_light);
            World::RemoveEntity(entity_camera);
            return false;
        }

        if (!CopyTextureToBuffer(frame_output, staging.get(), out_error))
        {
            World::RemoveEntity(entity_cube);
            World::RemoveEntity(entity_light);
            World::RemoveEntity(entity_camera);
            return false;
        }

        void* mapped_data = staging->GetMappedData();
        if (!mapped_data)
        {
            out_error = "Staging buffer not mappable";
            World::RemoveEntity(entity_cube);
            World::RemoveEntity(entity_light);
            World::RemoveEntity(entity_camera);
            return false;
        }

        uint32_t width = frame_output->GetWidth();
        uint32_t height = frame_output->GetHeight();
        uint32_t bits_per_channel = frame_output->GetBitsPerChannel();
        uint32_t channel_count = frame_output->GetChannelCount();

        ImageImporter::Save("smoke_test_render.exr", width, height, channel_count, bits_per_channel, mapped_data);

        if (!ValidateCenterPixel(mapped_data, width, height, bits_per_channel, channel_count))
        {
            out_error = "Center pixel is black (render failed or empty scene). Screenshot saved to smoke_test_render.exr";
        }

        staging = nullptr;
        World::RemoveEntity(entity_cube);
        World::RemoveEntity(entity_light);
        World::RemoveEntity(entity_camera);

        return true;
    }
}
