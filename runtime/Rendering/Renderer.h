/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ====================
#include "Renderer_Definitions.h"
#include "../RHI/RHI_Texture.h"
#include "../Math/Rectangle.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"
#include "../Math/Plane.h"
#include <unordered_map>
#include "Event.h"
#include "Mesh.h"
//===============================

namespace Spartan
{
    //= FWD DECLARATIONS =
    class Entity;
    class Camera;
    class Light;
    class Environment;
    namespace Math
    {
        class BoundingBox;
        class Frustum;
    }
    //====================

    class SP_CLASS Renderer
    {
    public:
        static void Initialize();
        static void Shutdown();
        static void Tick();

        // Primitive rendering (excellent for debugging)
        static void DrawLine(const Math::Vector3& from, const Math::Vector3& to, const Math::Vector4& color_from = DEBUG_COLOR, const Math::Vector4& color_to = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        static void DrawTriangle(const Math::Vector3& v0, const Math::Vector3& v1, const Math::Vector3& v2, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        static void DrawRectangle(const Math::Rectangle& rectangle, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        static void DrawBox(const Math::BoundingBox& box, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        static void DrawCircle(const Math::Vector3& center, const Math::Vector3& axis, const float radius, uint32_t segment_count, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        static void DrawSphere(const Math::Vector3& center, float radius, uint32_t segment_count, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        static void DrawDirectionalArrow(const Math::Vector3& start, const Math::Vector3& end, float arrow_size, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        static void DrawPlane(const Math::Plane& plane, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);

        // Options
        template<typename T>
        static T GetOption(const RendererOption option) { return static_cast<T>(GetOptions()[static_cast<uint32_t>(option)]); }
        static void SetOption(RendererOption option, float value);
        static std::array<float, 34>& GetOptions();
        static void SetOptions(std::array<float, 34> options);

        // Swapchain
        static RHI_SwapChain* GetSwapChain();
        static void Present();

        // Misc
        static void Flush();
        static void Pass_CopyToBackbuffer();
        static void SetGlobalShaderResources(RHI_CommandList* cmd_list);
        static void RequestTextureMipGeneration(std::shared_ptr<RHI_Texture> texture);
        static uint64_t GetFrameNum();
        static RHI_Api_Type GetRhiApiType();

        //= RESOLUTION/SIZE =============================================================================
        // Viewport
        static const RHI_Viewport& GetViewport();
        static void SetViewport(float width, float height);

        // Resolution render
        static const Math::Vector2& GetResolutionRender();
        static void SetResolutionRender(uint32_t width, uint32_t height, bool recreate_resources = true);

        // Resolution output
        static const Math::Vector2& GetResolutionOutput();
        static void SetResolutionOutput(uint32_t width, uint32_t height, bool recreate_resources = true);
        //===============================================================================================

        //= ENVIRONMENT ==================================================
        static const std::shared_ptr<RHI_Texture> GetEnvironmentTexture();
        static void SetEnvironment(Environment* environment);
        //================================================================

        //= RHI RESOURCES====================
        static RHI_CommandList* GetCmdList();
        //===================================
 
        //= RESOURCES ====================================================================================
        static RHI_Texture* GetFrameTexture();
        static std::shared_ptr<Camera> GetCamera();
        static std::unordered_map<RendererEntity, std::vector<std::shared_ptr<Entity>>>& GetEntities();

        // Get all
        static std::array<std::shared_ptr<RHI_Texture>, 26>& GetRenderTargets();
        static std::array<std::shared_ptr<RHI_Shader>, 47>& GetShaders();
        static std::array<std::shared_ptr<RHI_Sampler>, 7>& GetSamplers();
        static std::array<std::shared_ptr<RHI_Texture>, 9>& GetStandardTextures();
        static std::array<std::shared_ptr<Mesh>, 5>& GetStandardMeshes();

        // Get individual
        static std::shared_ptr<RHI_Texture> GetRenderTarget(const RendererTexture type);
        static std::shared_ptr<RHI_Shader> GetShader(const RendererShader type);
        static std::shared_ptr<RHI_Sampler> GetSampler(const RendererSampler type);
        static std::shared_ptr<RHI_Texture> GetStandardTexture(const RendererStandardTexture type);
        static std::shared_ptr<Mesh> GetStandardMesh(const RendererStandardMesh type);
        //================================================================================================

    private:
        // Constant buffers
        static void UpdateConstantBufferFrame(RHI_CommandList* cmd_list);
        static void UpdateConstantBufferUber(RHI_CommandList* cmd_list);
        static void UpdateConstantBufferLight(RHI_CommandList* cmd_list, const std::shared_ptr<Light> light, const RHI_Shader_Type scope);
        static void UpdateConstantBufferMaterial(RHI_CommandList* cmd_list);

        // Resource creation
        static void CreateConstantBuffers();
        static void CreateStructuredBuffers();
        static void CreateDepthStencilStates();
        static void CreateRasterizerStates();
        static void CreateBlendStates();
        static void CreateFonts();
        static void CreateStandardMeshes();
        static void CreateStandardTextures();
        static void CreateShaders();
        static void CreateSamplers(const bool create_only_anisotropic = false);
        static void CreateRenderTextures(const bool create_render, const bool create_output, const bool create_fixed, const bool create_dynamic);

        // Passes
        static void Pass_Main(RHI_CommandList* cmd_list);
        static void Pass_ShadowMaps(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        static void Pass_ReflectionProbes(RHI_CommandList* cmd_list);
        static void Pass_Depth_Prepass(RHI_CommandList* cmd_list);
        static void Pass_GBuffer(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        static void Pass_Ssgi(RHI_CommandList* cmd_list);
        static void Pass_Ssr(RHI_CommandList* cmd_list, RHI_Texture* tex_in);
        static void Pass_PostProcess(RHI_CommandList* cmd_list);
        static void Pass_ToneMappingGammaCorrection(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_Fxaa(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_FilmGrain(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_ChromaticAberration(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_MotionBlur(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_DepthOfField(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_Debanding(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_Bloom(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_Blur_Gaussian(RHI_CommandList* cmd_list, RHI_Texture* tex_in, const bool depth_aware, const float radius, const float sigma, const uint32_t mip = rhi_all_mips);
        static void Pass_Lines(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        static void Pass_DebugMeshes(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        static void Pass_Outline(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        static void Pass_Icons(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        static void Pass_PeformanceMetrics(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        static void Pass_BrdfSpecularLut(RHI_CommandList* cmd_list);
        static void Pass_Copy(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out, const bool bilinear);
        // Lighting
        static void Pass_Light(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        static void Pass_Light_Composition(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass);
        static void Pass_Light_ImageBased(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass);
        // AMD FidelityFX
        static void Pass_Ffx_Cas(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_Ffx_Spd(RHI_CommandList* cmd_list, RHI_Texture* tex);
        static void Pass_Ffx_Fsr2(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);

        // Event handlers
        static void OnWorldResolved(sp_variant data);
        static void OnClear();
        static void OnFullScreenToggled();

        // Misc
        static void SortRenderables(std::vector<std::shared_ptr<Entity>>* renderables);
        static bool IsCallingFromOtherThread();
        static void OnResourceSafe(RHI_CommandList* cmd_list);

        // Lines
        static void Lines_PreMain();
        static void Lines_PostMain();
    };
}
