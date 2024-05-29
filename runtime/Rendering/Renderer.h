/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "Event.h"
#include "Mesh.h"
#include "Renderer_Buffers.h"
#include "Font/Font.h"
#include <unordered_map>
#include <atomic>
//===============================

namespace Spartan
{
    //= FWD DECLARATIONS =
    class Entity;
    class Camera;
    class Light;
    namespace Math
    {
        class BoundingBox;
        class Frustum;
    }
    //====================

    class SP_CLASS Renderer
    {
    public:
        // core
        static void Initialize();
        static void Shutdown();
        static void Tick();

        // primitive rendering (useful for debugging)
        static void DrawLine(const Math::Vector3& from, const Math::Vector3& to, const Color& color_from = Color::standard_renderer_lines, const Color& color_to = Color::standard_renderer_lines, const float duration = 0.0f, const bool depth = true);
        static void DrawTriangle(const Math::Vector3& v0, const Math::Vector3& v1, const Math::Vector3& v2, const Color& color = Color::standard_renderer_lines, const float duration = 0.0f, const bool depth = true);
        static void DrawBox(const Math::BoundingBox& box, const Color& color = Color::standard_renderer_lines, const float duration = 0.0f, const bool depth = true);
        static void DrawCircle(const Math::Vector3& center, const Math::Vector3& axis, const float radius, uint32_t segment_count, const Color& color = Color::standard_renderer_lines, const float duration = 0.0f, const bool depth = true);
        static void DrawSphere(const Math::Vector3& center, float radius, uint32_t segment_count, const Color& color = Color::standard_renderer_lines, const float duration = 0.0f, const bool depth = true);
        static void DrawDirectionalArrow(const Math::Vector3& start, const Math::Vector3& end, float arrow_size, const Color& color = Color::standard_renderer_lines, const float duration = 0.0f, const bool depth = true);
        static void DrawPlane(const Math::Plane& plane, const Color& color = Color::standard_renderer_lines, const float duration = 0.0f, const bool depth = true);
        static void DrawString(const std::string& text, const Math::Vector2& position_screen_percentage);

        // options
        template<typename T>
        static T GetOption(const Renderer_Option option) { return static_cast<T>(GetOptions()[option]); }
        static void SetOption(Renderer_Option option, float value);
        static std::unordered_map<Renderer_Option, float>& GetOptions();
        static void SetOptions(const std::unordered_map<Renderer_Option, float>& options);

        // swapchain
        static RHI_SwapChain* GetSwapChain();
        static void BlitToBackBuffer(RHI_CommandList* cmd_list, RHI_Texture* texture);
        static void Present();

        // misc
        static RHI_CommandList* GetCmdList();
        static void SetStandardResources(RHI_CommandList* cmd_list);
        static uint64_t GetFrameNum();
        static RHI_Api_Type GetRhiApiType();
        static void Screenshot(const std::string& file_path);
        static void SetEntities(std::vector<std::shared_ptr<Entity>>& entities);
        static bool CanUseCmdList();

        //= RESOLUTION/SIZE =============================================================================
        // viewport
        static const RHI_Viewport& GetViewport();
        static void SetViewport(float width, float height);

        // resolution render
        static const Math::Vector2& GetResolutionRender();
        static void SetResolutionRender(uint32_t width, uint32_t height, bool recreate_resources = true);

        // resolution output
        static const Math::Vector2& GetResolutionOutput();
        static void SetResolutionOutput(uint32_t width, uint32_t height, bool recreate_resources = true);
        //===============================================================================================
  
        //= RESOURCES ==========================================================================================================
        static RHI_Texture* GetFrameTexture();
        static std::shared_ptr<Camera> GetCamera();
        static std::unordered_map<Renderer_Entity, std::vector<std::shared_ptr<Entity>>>& GetEntities();
        static std::vector<std::shared_ptr<Entity>> GetEntitiesLights();

        // get all
        static std::array<std::shared_ptr<RHI_Texture>, static_cast<uint32_t>(Renderer_RenderTarget::max)>& GetRenderTargets();
        static std::array<std::shared_ptr<RHI_Shader>, static_cast<uint32_t>(Renderer_Shader::max)>& GetShaders();
        static std::array<std::shared_ptr<RHI_StructuredBuffer>, 3>& GetStructuredBuffers();

        // get individual
        static std::shared_ptr<RHI_RasterizerState> GetRasterizerState(const Renderer_RasterizerState type);
        static std::shared_ptr<RHI_DepthStencilState> GetDepthStencilState(const Renderer_DepthStencilState type);
        static std::shared_ptr<RHI_BlendState> GetBlendState(const Renderer_BlendState type);
        static std::shared_ptr<RHI_Texture> GetRenderTarget(const Renderer_RenderTarget type);
        static std::shared_ptr<RHI_Shader> GetShader(const Renderer_Shader type);
        static std::shared_ptr<RHI_Sampler> GetSampler(const Renderer_Sampler type);
        static std::shared_ptr<RHI_ConstantBuffer>& GetConstantBufferFrame();
        static std::shared_ptr<RHI_StructuredBuffer> GetStructuredBuffer(const Renderer_StructuredBuffer type);
        static std::shared_ptr<RHI_Texture> GetStandardTexture(const Renderer_StandardTexture type);
        static std::shared_ptr<Mesh> GetStandardMesh(const MeshType type);
        static std::shared_ptr<Font>& GetFont();
        static std::shared_ptr<Material> GetStandardMaterial();
        //======================================================================================================================

    private:
        // constant and push constant buffers
        static void UpdateConstantBufferFrame(RHI_CommandList* cmd_list);

        // resource creation
        static void CreateBuffers();
        static void CreateDepthStencilStates();
        static void CreateRasterizerStates();
        static void CreateBlendStates();
        static void CreateShaders();
        static void CreateSamplers();
        static void CreateRenderTargets(const bool create_render, const bool create_output, const bool create_dynamic);
        static void CreateFonts();
        static void CreateStandardMeshes();
        static void CreateStandardTextures();
        static void CreateStandardMaterials();

        // passes - core
        static void ProduceFrame(RHI_CommandList* cmd_list);
        static void Pass_VariableRateShading(RHI_CommandList* cmd_list);
        static void Pass_ShadowMaps(RHI_CommandList* cmd_list, const bool is_transparent_pass = false);
        static void Pass_Visibility(RHI_CommandList* cmd_list);
        static void Pass_Depth_Prepass(RHI_CommandList* cmd_list, const bool is_transparent_pass = false);
        static void Pass_GBuffer(RHI_CommandList* cmd_list, const bool is_transparent_pass = false);
        static void Pass_Ssgi(RHI_CommandList* cmd_list);
        static void Pass_Ssr(RHI_CommandList* cmd_list, RHI_Texture* tex_in, const bool is_transparent_pass = false);
        static void Pass_Sss(RHI_CommandList* cmd_list);
        static void Pass_Skysphere(RHI_CommandList* cmd_list);
        static void Pass_Light_Integration_BrdfSpecularLut(RHI_CommandList* cmd_list);
        static void Pass_Light_Integration_EnvironmentPrefilter(RHI_CommandList* cmd_list);
        static void Pass_Blur_Gaussian(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_radius, const Renderer_Shader shader_type, const float radius, const uint32_t mip = rhi_all_mips);
        // passes - debug/editor
        static void Pass_Grid(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        static void Pass_Lines(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        static void Pass_Outline(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        static void Pass_Icons(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        static void Pass_Text(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        // passes - post-process
        static void Pass_PostProcess(RHI_CommandList* cmd_list);
        static void Pass_Output(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_Fxaa(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_FilmGrain(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_ChromaticAberration(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_MotionBlur(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_DepthOfField(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_Bloom(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_Antiflicker(RHI_CommandList* cmd_list, RHI_Texture* tex_in);
        // passes - lighting
        static void Pass_Light(RHI_CommandList* cmd_list, const bool is_transparent_pass = false);
        static void Pass_Light_Composition(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass = false);
        static void Pass_Light_ImageBased(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass = false);
        // passes - amd fidelityfx
        static void Pass_Ffx_Cas(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_Ffx_Spd(RHI_CommandList* cmd_list, RHI_Texture* tex, const Renderer_DownsampleFilter filter, const uint32_t mip_start = 0);
        static void Pass_Ffx_Fsr2(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);

        // event handlers
        static void OnClear();
        static void OnFullScreenToggled();
        static void OnSyncPoint(RHI_CommandList* cmd_list);

        // misc
        static void AddLinesToBeRendered();
        static void SetGbufferTextures(RHI_CommandList* cmd_list);
        static void DestroyResources();

        // bindless
        static void BindlessUpdateMaterials();
        static void BindlessUpdateLights();

        // misc
        static std::unordered_map<Renderer_Entity, std::vector<std::shared_ptr<Entity>>> m_renderables;
        static Cb_Frame m_cb_frame_cpu;
        static Pcb_Pass m_pcb_pass_cpu;
        static std::shared_ptr<RHI_VertexBuffer> m_vertex_buffer_lines;
        static std::vector<RHI_Vertex_PosCol> m_line_vertices;
        static std::vector<float> m_lines_duration;
        static uint32_t m_lines_index_depth_off;
        static uint32_t m_lines_index_depth_on;
        static uint32_t m_resource_index;
        static std::atomic<bool> m_resources_created;
        static std::atomic<uint32_t> m_environment_mips_to_filter_count;
        static std::mutex m_mutex_renderables;
    };
}
