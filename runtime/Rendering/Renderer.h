/*
Copyright(c) 2016-2025 Panos Karabelas

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
#include "../Math/Vector3.h"
#include "../Math/Plane.h"
#include "Mesh.h"
#include "Renderer_Buffers.h"
#include "../Font/Font.h"
#include <unordered_map>
#include <atomic>
//===============================

namespace spartan
{
    class Material;
    class Entity;
    class Camera;
    class Light;
    class Renderable;
    namespace math
    {
        class BoundingBox;
        class Frustum;
    }

    class Renderer
    {
    public:
        // core
        static void Initialize();
        static void Shutdown();
        static void Tick();

        // primitive rendering (useful for debugging)
        static void DrawLine(const math::Vector3& from, const math::Vector3& to, const Color& color_from = Color::standard_renderer_lines, const Color& color_to = Color::standard_renderer_lines);
        static void DrawTriangle(const math::Vector3& v0, const math::Vector3& v1, const math::Vector3& v2, const Color& color = Color::standard_renderer_lines);
        static void DrawBox(const math::BoundingBox& box, const Color& color = Color::standard_renderer_lines);
        static void DrawCircle(const math::Vector3& center, const math::Vector3& axis, const float radius, uint32_t segment_count, const Color& color = Color::standard_renderer_lines);
        static void DrawSphere(const math::Vector3& center, float radius, uint32_t segment_count, const Color& color = Color::standard_renderer_lines);
        static void DrawDirectionalArrow(const math::Vector3& start, const math::Vector3& end, float arrow_size, const Color& color = Color::standard_renderer_lines);
        static void DrawPlane(const math::Plane& plane, const Color& color = Color::standard_renderer_lines);
        static void DrawString(const std::string& text, const math::Vector2& position_screen_percentage);

        // options
        template<typename T>
        static T GetOption(const Renderer_Option option) { return static_cast<T>(GetOptions()[option]); }
        static void SetOption(Renderer_Option option, float value);
        static std::unordered_map<Renderer_Option, float>& GetOptions();
        static void SetOptions(const std::unordered_map<Renderer_Option, float>& options);

        // swapchain
        static RHI_SwapChain* GetSwapChain();
        static void BlitToBackBuffer(RHI_CommandList* cmd_list, RHI_Texture* texture);
        static void SubmitAndPresent();

        // misc
        static void SetStandardResources(RHI_CommandList* cmd_list);
        static uint64_t GetFrameNumber();
        static RHI_Api_Type GetRhiApiType();
        static void Screenshot(const std::string& file_path);
        static void SetEntities(std::vector<std::shared_ptr<Entity>>& entities);

        // wind
        static const math::Vector3& GetWind();
        static void SetWind(const math::Vector3& wind);

        // viewport
        static const RHI_Viewport& GetViewport();
        static void SetViewport(float width, float height);

        // resolution render
        static const math::Vector2& GetResolutionRender();
        static void SetResolutionRender(uint32_t width, uint32_t height, bool recreate_resources = true);

        // resolution output
        static const math::Vector2& GetResolutionOutput();
        static void SetResolutionOutput(uint32_t width, uint32_t height, bool recreate_resources = true);

        // get all
        static std::array<std::shared_ptr<RHI_Texture>, static_cast<uint32_t>(Renderer_RenderTarget::max)>& GetRenderTargets();
        static std::array<std::shared_ptr<RHI_Shader>, static_cast<uint32_t>(Renderer_Shader::max)>& GetShaders();
        static std::array<std::shared_ptr<RHI_Buffer>, static_cast<uint32_t>(Renderer_Buffer::Max)>& GetStructuredBuffers();
        static std::array<std::shared_ptr<RHI_Sampler>, static_cast<uint32_t>(Renderer_Sampler::Max)>& GetSamplers();

        // get individual
        static RHI_RasterizerState* GetRasterizerState(const Renderer_RasterizerState type);
        static RHI_DepthStencilState* GetDepthStencilState(const Renderer_DepthStencilState type);
        static RHI_BlendState* GetBlendState(const Renderer_BlendState type);
        static RHI_Texture* GetRenderTarget(const Renderer_RenderTarget type);
        static RHI_Shader* GetShader(const Renderer_Shader type);
        static RHI_Buffer* GetBuffer(const Renderer_Buffer type);
        static RHI_Texture* GetStandardTexture(const Renderer_StandardTexture type);
        static std::shared_ptr<Mesh>& GetStandardMesh(const MeshType type);
        static std::shared_ptr<Font>& GetFont();
        static std::shared_ptr<Material>& GetStandardMaterial();

    private:
        static void UpdateFrameConstantBuffer(RHI_CommandList* cmd_list);

        // resources
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
        static void WaitForValidResources();

        // passes - core
        static void ProduceFrame(RHI_CommandList* cmd_list_graphics, RHI_CommandList* cmd_list_compute);
        static void Pass_VariableRateShading(RHI_CommandList* cmd_list);
        static void Pass_ShadowMaps(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        static void BuildDrawCallsAndOccluders(RHI_CommandList* cmd_list);
        static void Pass_Occlusion(RHI_CommandList* cmd_list);
        static void Pass_Depth_Prepass(RHI_CommandList* cmd_list);
        static void Pass_GBuffer(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        static void Pass_Ssao(RHI_CommandList* cmd_list);
        static void Pass_Ssr(RHI_CommandList* cmd_list);
        static void Pass_Sss(RHI_CommandList* cmd_list);
        static void Pass_Skysphere(RHI_CommandList* cmd_list);
        // passes - lighting
        static void Pass_Light(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        static void Pass_Light_GlobalIllumination(RHI_CommandList* cmd_list);
        static void Pass_Light_Composition(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        static void Pass_Light_ImageBased(RHI_CommandList* cmd_list);
        static void Pass_Light_Integration_BrdfSpecularLut(RHI_CommandList* cmd_list);
        static void Pass_Light_Integration_EnvironmentPrefilter(RHI_CommandList* cmd_list);
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
        static void Pass_Sharpening(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_Upscale(RHI_CommandList* cmd_list);
        // passes - utility
        static void Pass_Blit(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_Downscale(RHI_CommandList* cmd_list, RHI_Texture* tex, const Renderer_DownsampleFilter filter);
        static void Pass_Blur(RHI_CommandList* cmd_list, RHI_Texture* tex_in, const float radius, const uint32_t mip = rhi_all_mips);

        // event handlers
        static void OnFullScreenToggled();
        static void UpdateBuffers(RHI_CommandList* cmd_list);

        // bindless
        static void UpdateBindlessBuffers(RHI_CommandList* cmd_list);
        static void BindlessUpdateMaterialsParameters(RHI_CommandList* cmd_list);
        static void BindlessUpdateLights(RHI_CommandList* cmd_lis);
        static void BindlessUpdateOccludersAndOccludes(RHI_CommandList* cmd_list);

        // misc
        static void AddLinesToBeRendered();
        static void SetGbufferTextures(RHI_CommandList* cmd_list);
        static void DestroyResources();

        // misc
        static Cb_Frame m_cb_frame_cpu;
        static Pcb_Pass m_pcb_pass_cpu;
        static std::shared_ptr<RHI_Buffer> m_lines_vertex_buffer;
        static std::vector<RHI_Vertex_PosCol> m_lines_vertices;
        static uint32_t m_resource_index;
        static std::atomic<bool> m_initialized_resources;
        static std::atomic<uint32_t> m_environment_mips_to_filter_count;
        static std::mutex m_mutex_renderables;
        static std::array<Renderer_DrawCall, renderer_max_entities> m_draw_calls;
        static uint32_t m_draw_call_count;
        static bool m_transparents_present;

        // bindless
        static std::array<RHI_Texture*, rhi_max_array_size> m_bindless_textures;
        static std::array<Sb_Light, rhi_max_array_size> m_bindless_lights;
        static std::array<Sb_Aabb, rhi_max_array_size> m_bindless_aabbs;
        static bool m_bindless_samplers_dirty;
        static bool m_bindless_abbs_dirty;
        static bool m_bindless_materials_dirty;
        static bool m_bindless_lights_dirty;
    };
}
