/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ================================
#include "pch.h"
#include "SmokeTest.h"
#include "../core/Engine.h"
#include "../core/Timer.h"
#include "../core/Window.h"
#include "../world/Environment.h"
#include "../io/pugixml.hpp"
#include "../logging/Log.h"
#include "../rendering/Renderer.h"
#include "../rendering/Material.h"
#include "../resource/import/ImageImporter.h"
#include "../resource/IResource.h"
#include "../rhi/RHI_Shader.h"
#include "../rhi/RHI_InputLayout.h"
#include "../rhi/RHI_Texture.h"
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_CommandList.h"
#include "../rhi/RHI_Device.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Camera.h"
#include "../world/components/Light.h"
#include "../world/components/Render.h"
#include "../world/components/Physics.h"
#include "../file_system/FileSystem.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
//===========================================

namespace spartan
{
    bool SmokeTest::m_delayed_tests_pending = false;
    uint32_t SmokeTest::m_test_count = 0;
    uint32_t SmokeTest::m_passed_count = 0;
    std::string SmokeTest::m_error;
    bool SmokeTest::m_all_tests_passed = true;
    double SmokeTest::m_start_time_ms = 0.0;

    namespace
    {
        Entity* test_camera = nullptr;
        Entity* test_light = nullptr;
        Entity* test_cube = nullptr;
        uint64_t previous_camera_id = 0, setup_frame = 0;
        bool render_test_ready = false;
        int play_test_stage = 0;
        uint64_t parent_id = 0, child_id = 0, deleted_id = 0, spawned_id = 0;
        double play_test_start = 0.0;
        std::string failures;

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
        {
            RunInitialTests();
        }
    }

    void SmokeTest::Tick()
    {
        if (!m_delayed_tests_pending)
        {
            return;
        }

        if (play_test_stage && Timer::GetTimeMs() - play_test_start > 60000.0)
        {
            m_delayed_tests_pending = false;
            RunTest("World.PlayRestore", [](std::string& error) { error = "Play restoration timed out"; return false; });
            RunDelayedTests();
            return;
        }
        if (World::IsLoadingFromFile() || World::IsPlayBooting()) return;
        if (play_test_stage == 1)
        {
            Entity* parent = World::CreateEntity(); parent_id = parent->GetObjectId();
            Entity* child = World::CreateEntity(); child_id = child->GetObjectId();
            child->SetParent(parent);
            child->SetPositionLocal(math::Vector3(1, 2, 3));
            child->AddComponent<Light>()->SetIntensity(1180.0f);
            Entity* deleted = World::CreateEntity(); deleted_id = deleted->GetObjectId();
            deleted->SetObjectName("play_restore_deleted");
            Engine::SetFlag(EngineMode::Playing, true);
            play_test_stage = 2;
            return;
        }
        if (play_test_stage == 2)
        {
            if (Entity* child = World::GetEntityById(child_id))
            {
                child->SetParent(nullptr);
                child->SetPositionLocal(math::Vector3(9, 8, 7));
                child->SetActive(false);
                child->GetComponent<Light>()->SetIntensity(25.0f);
            }
            World::RemoveEntity(World::GetEntityById(deleted_id));
            spawned_id = World::CreateEntity()->GetObjectId();
            play_test_stage = 3;
            return;
        }
        if (play_test_stage == 3)
        {
            // Allow the previous frame's deletion to be committed before stopping.
            Engine::SetFlag(EngineMode::Playing, false);
            play_test_stage = 4;
            return;
        }
        if (play_test_stage == 4)
        {
            RunTest("World.PlayRestore", Test_PlayRestore);
            for (uint64_t id : {child_id, parent_id, deleted_id, spawned_id})
                if (Entity* entity = World::GetEntityById(id)) World::RemoveEntity(entity);
            play_test_stage = 0;
            m_delayed_tests_pending = false;
            RunDelayedTests();
            return;
        }

        Material* standard_material = Renderer::GetStandardMaterial().get();
        if (!test_camera && standard_material && standard_material->GetResourceState() >= ResourceState::PreparedForGpu)
        {
            if (Entity* previous = World::GetActiveCameraOverride()) previous_camera_id = previous->GetObjectId();
            test_camera = CreateTestCamera("SmokeTest_Camera", math::Vector3(0.0f, 10.0f, -5.0f));
            test_light = CreateTestLight("SmokeTest_Light", math::Vector3(0.0f, 10.0f, 0.0f), 0.0f);
            test_cube = CreateTestCube("SmokeTest_Cube", math::Vector3(0.0f, 10.0f, 0.0f));
            auto material = std::make_shared<Material>();
            material->SetObjectName("smoke_test_green");
            material->SetColor(Color(0.0f, 1.0f, 0.0f, 1.0f));
            // Full strength is 100,000 nits; keep this fixture below tonemapper saturation.
            material->SetProperty(MaterialProperty::EmissiveFromAlbedo, 0.002f);
            test_cube->GetComponent<Render>()->SetMaterial(material);
            World::SetActiveCamera(test_camera);
            setup_frame = Renderer::GetFrameNumber();
            return;
        }
        if (test_cube && test_cube->GetComponent<Render>()->GetMaterial()->GetResourceState() < ResourceState::PreparedForGpu)
            setup_frame = Renderer::GetFrameNumber();
        // The editor can render its first UI frame while scene shaders are still queued.
        for (const auto& shader : Renderer::GetShaders())
            if (shader && !shader->IsCompiled()) setup_frame = Renderer::GetFrameNumber();
        render_test_ready = test_camera && Renderer::GetFrameNumber() >= setup_frame + 8;
        if (render_test_ready || Timer::GetTimeMs() - m_start_time_ms > 60000.0)
        {
            RunTest("Render.BasicCube", Test_Render_BasicCube);
            World::SetActiveCamera(World::GetEntityById(previous_camera_id));
            for (Entity* entity : {test_cube, test_light, test_camera}) if (entity) World::RemoveEntity(entity);
            test_cube = test_light = test_camera = nullptr;
            Engine::SetFlag(EngineMode::Playing, false);
            play_test_stage = 1;
            play_test_start = Timer::GetTimeMs();
        }
    }

    void SmokeTest::RunTest(const char* name, bool (*test_func)(std::string&))
    {
        m_test_count++;
        m_error.clear();
        SP_LOG_INFO("Running: %s...", name);

        if (test_func(m_error))
        {
            m_passed_count++;
            SP_LOG_INFO("  ✓ PASSED: %s", name);
        }
        else
        {
            m_all_tests_passed = false;
            failures += std::string(name) + ": " + m_error + "\n";
            SP_LOG_ERROR("  ✗ FAILED: %s - %s", name, m_error.c_str());
        }
    }

    void SmokeTest::RunInitialTests()
    {
        SP_LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        SP_LOG_INFO("Starting Smoke Tests...");
        SP_LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        m_start_time_ms = Timer::GetTimeMs();
        m_test_count = 0;
        m_passed_count = 0;
        m_error.clear();
        m_all_tests_passed = true;
        failures.clear();

        RunTest("Render.PixelValidation", [](std::string& error)
        {
            uint16_t pixel[4] = {0, 0, 0, 0x3c00}; // opaque black must fail
            bool valid = !ValidateCenterPixel(pixel, 1, 1, 16, 4);
            pixel[0] = pixel[1] = pixel[2] = 0x3c00; // white must fail
            valid &= !ValidateCenterPixel(pixel, 1, 1, 16, 4);
            pixel[0] = pixel[2] = 0; // green must pass
            valid &= ValidateCenterPixel(pixel, 1, 1, 16, 4);
            pixel[1] = 0x7e00; // NaN must fail
            valid &= !ValidateCenterPixel(pixel, 1, 1, 16, 4);
            if (!valid) error = "Pixel validation accepted invalid output or rejected green";
            return valid;
        });
        RunTest("World.EnvironmentWeather", Test_EnvironmentWeather);
        RunTest("World.ComponentCopy", Test_ComponentCopy);
        RunTest("RHI.BackendInitialization",  Test_RHI_BackendInitialization);
        RunTest("RHI.MemoryAllocation",          Test_RHI_MemoryAllocation);
        RunTest("Shader.CompilationPipeline",   Test_Shader_CompilationPipeline);
        RunTest("Renderer.PipelineStates",       Test_Renderer_PipelineStates);
        RunTest("RHI.CommandListRecording",   Test_RHI_CommandListRecording);
        RunTest("RHI.ResourceTransitions",      Test_RHI_ResourceTransitions);
        RunTest("Threading.ResourceCreation",  Test_Threading_ResourceCreation);

        m_delayed_tests_pending = true;
    }

    void SmokeTest::RunDelayedTests()
    {
        double elapsed_ms = Timer::GetTimeMs() - m_start_time_ms;
        std::ofstream file("ci_test.txt");
        if (file.is_open())
        {
            if (m_all_tests_passed)
            {
                file << "0";
                SP_LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                SP_LOG_INFO("Smoke Tests: %d/%d PASSED in %.2f ms", m_passed_count, m_test_count, elapsed_ms);
                SP_LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            }
            else
            {
                file << "1" << std::endl;
                file << failures;
                SP_LOG_ERROR("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                SP_LOG_ERROR("Smoke Tests: %d/%d FAILED in %.2f ms", m_passed_count, m_test_count, elapsed_ms);
                SP_LOG_ERROR("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            }
            file.close();
        }
        if (Engine::HasArgument("--ci-exit")) Window::Close();
    }

    bool SmokeTest::Test_EnvironmentWeather(std::string& out_error)
    {
        const auto before = Environment::GetSettings();
        pugi::xml_document legacy;
        legacy.load_string("<World><Entities><Entity><light light_type='0' rain='0.6' cloud_coverage='0.3'/></Entity></Entities></World>");
        auto world = legacy.child("World");
        Environment::Load(world);
        bool valid = std::abs(Environment::GetRain() - 0.6f) < 0.001f;
        auto environment = world.append_child("Environment");
        environment.append_attribute("rain") = 0.2f;
        environment.append_attribute("cloud_coverage") = 0.1f;
        Environment::Load(world);
        valid &= std::abs(Environment::GetRain() - 0.2f) < 0.001f;
        pugi::xml_document saved;
        auto saved_world = saved.append_child("World");
        auto saved_environment = saved_world.append_child("Environment");
        Environment::Save(saved_environment);
        Environment::SetRain(0.0f);
        Environment::Load(saved_world);
        valid &= std::abs(Environment::GetRain() - 0.2f) < 0.001f;
        Environment::SetSettings(before);
        if (!valid) out_error = "Environment migration, explicit override or roundtrip failed";
        return valid;
    }

    bool SmokeTest::Test_ComponentCopy(std::string& out_error)
    {
        Entity* source = World::CreateEntity();
        Camera* camera = source->AddComponent<Camera>();
        auto settings = camera->GetSettings();
        settings.fov_horizontal_rad = 0.9f;
        settings.near_plane = 0.5f;
        settings.exposure_mode = CameraExposureMode::manual;
        settings.mouse_sensitivity = 0.7f;
        camera->ApplySettings(settings);
        Entity* clone = source->Clone();
        const auto copy = clone->GetComponent<Camera>()->GetSettings();
        bool valid = std::abs(copy.fov_horizontal_rad - settings.fov_horizontal_rad) < 0.001f &&
            copy.near_plane == settings.near_plane && copy.exposure_mode == settings.exposure_mode &&
            copy.mouse_sensitivity == settings.mouse_sensitivity;
        World::RemoveEntity(source);
        World::RemoveEntity(clone);
        if (!valid) out_error = "Camera clone lost authored settings";
        return valid;
    }

    bool SmokeTest::Test_PlayRestore(std::string& out_error)
    {
        Entity* child = World::GetEntityById(child_id);
        Entity* parent = World::GetEntityById(parent_id);
        Entity* deleted = World::GetEntityById(deleted_id);
        Light* light = child ? child->GetComponent<Light>() : nullptr;
        const bool valid = child && parent && deleted && light && child->GetParent() == parent && child->IsActive() &&
            child->GetPositionLocal() == math::Vector3(1, 2, 3) && light->GetIntensityPhotometric() == 1180.0f &&
            !World::GetEntityById(spawned_id) && !Engine::IsFlagSet(EngineMode::Playing);
        if (!valid) out_error = "Play stop failed to restore authored entities, hierarchy, active state or component settings";
        return valid;
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

        if (!Engine::IsHeadless())
        {
            RHI_SwapChain* swap_chain = RHI_Device::GetSwapChain();
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
            RHI_CommandList::ClearTexture(texture.get(), Color(1, 0, 0, 1));

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
            RHI_Texture_Srv | RHI_Texture_Uav | RHI_Texture_Rtv | RHI_Texture_ClearBlit,
            "smoke_test_barrier"
        );

        if (RHI_CommandList* cmd = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics))
        {
            RHI_CommandList::ClearTexture(texture.get(), Color(1, 0, 0, 1));
            RHI_CommandList::ClearTexture(texture.get(), Color(0, 1, 0, 1));
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
                    {
                        success_count++;
                    }
                    else
                    {
                        failure_count++;
                    }
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
        Camera* camera = entity->AddComponent<Camera>();
        camera->SetExposureMode(CameraExposureMode::manual);
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
        Render* render = entity->AddComponent<Render>();
        render->SetMesh(MeshType::Cube);
        render->SetMaterial(Renderer::GetStandardMaterial());
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

        const uint64_t data_size = texture->GetObjectSize(); // includes every mip copied by the RHI

        std::unique_ptr<RHI_Buffer> staging = std::make_unique<RHI_Buffer>(
            RHI_Buffer_Type::Readback,
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
            RHI_CommandList::CopyTextureToBuffer(texture, buffer);
            RHI_CommandList::ImmediateExecutionEnd(cmd_list);
            return true;
        }

        out_error = "Failed to begin immediate command list";
        return false;
    }

    bool SmokeTest::ValidateCenterPixel(void* data, uint32_t width, uint32_t height, uint32_t bits_per_channel, uint32_t channel_count)
    {
        // frame_output is RGBA16F. Validate finite RGB, never alpha or raw float bytes.
        if (!data || !width || !height || bits_per_channel != 16 || channel_count != 4) return false;
        const auto half_to_float = [](uint16_t value)
        {
            const int exponent = (value >> 10) & 31, mantissa = value & 1023;
            if (exponent == 31) return std::numeric_limits<float>::quiet_NaN();
            const float magnitude = exponent == 0 ? std::ldexp(static_cast<float>(mantissa), -24)
                : std::ldexp(static_cast<float>(mantissa + 1024), exponent - 25);
            return (value & 0x8000) ? -magnitude : magnitude;
        };
        const uint16_t* pixel = static_cast<const uint16_t*>(data) + (static_cast<size_t>(height / 2) * width + width / 2) * 4;
        const float r = half_to_float(pixel[0]), g = half_to_float(pixel[1]), b = half_to_float(pixel[2]);
        SP_LOG_INFO("Render fixture center RGB: %.5f, %.5f, %.5f", r, g, b);
        return std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && g > std::max(r, b) * 1.5f + 0.01f;
    }

    bool SmokeTest::Test_Render_BasicCube(std::string& out_error)
    {
        if (!render_test_ready)
        {
            out_error = "Timed out waiting for the render fixture";
            return false;
        }
        RHI_Texture* output = Renderer::GetRenderTarget(Renderer_RenderTarget::frame_output);
        auto staging = CreateStagingBuffer(output, out_error);
        if (!staging || !CopyTextureToBuffer(output, staging.get(), out_error)) return false;
        const bool valid = ValidateCenterPixel(staging->GetMappedData(), output->GetWidth(), output->GetHeight(),
            output->GetBitsPerChannel(), output->GetChannelCount());
        if (!valid) out_error = "Expected finite green RGB from the emissive cube after rendering";
        return valid;
    }
}
