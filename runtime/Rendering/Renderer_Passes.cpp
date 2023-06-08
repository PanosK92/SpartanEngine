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

//= INCLUDES ===================================
#include "pch.h"
#include "Grid.h"
#include "Font/Font.h"
#include "../Profiling/Profiler.h"
#include "../World/Entity.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Components/ReflectionProbe.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Implementation.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_FSR2.h"
#include "../RHI/RHI_StructuredBuffer.h"
#include "Renderer_ConstantBuffers.h"
#include "../RHI/RHI_SwapChain.h"
//==============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

// Macros to work around the verboseness of some C++ concepts.
#define thread_group_count_x(tex) static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex->GetWidth())  / m_thread_group_count))
#define thread_group_count_y(tex) static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex->GetHeight()) / m_thread_group_count))

namespace Spartan
{
    namespace
    {
        static const float m_thread_group_count = 8.0f;
    }

    // Misc
    extern Cb_Frame m_cb_frame_cpu;
    extern Cb_Uber m_cb_uber_cpu;
    extern Cb_Light m_cb_light_cpu;
    extern Cb_Material m_cb_material_cpu;
    extern vector<RHI_Vertex_PosCol> m_line_vertices;
    extern vector<float> m_lines_duration;
    extern uint32_t m_lines_index_depth_off;
    extern uint32_t m_lines_index_depth_on;
    extern shared_ptr<RHI_VertexBuffer> m_vertex_buffer_lines;
    extern array<Material*, m_max_material_instances> m_material_instances;
    extern shared_ptr<RHI_SwapChain> m_swap_chain;
    extern unordered_map<RendererEntity, vector<shared_ptr<Entity>>> m_renderables;
    extern unique_ptr<Font> m_font;
    extern unique_ptr<Grid> m_world_grid;
    extern RHI_CommandList* m_cmd_current;
    extern bool m_brdf_specular_lut_rendered;

    void Renderer::SetGlobalShaderResources(RHI_CommandList* cmd_list)
    {
        // Constant buffers
        cmd_list->SetConstantBuffer(RendererBindingsCb::frame,    RHI_Shader_Vertex | RHI_Shader_Pixel | RHI_Shader_Compute, GetConstantBuffer(RendererConstantBuffer::frame));
        cmd_list->SetConstantBuffer(RendererBindingsCb::uber,     RHI_Shader_Vertex | RHI_Shader_Pixel | RHI_Shader_Compute, GetConstantBuffer(RendererConstantBuffer::uber));
        cmd_list->SetConstantBuffer(RendererBindingsCb::light,    RHI_Shader_Compute,                                        GetConstantBuffer(RendererConstantBuffer::light));
        cmd_list->SetConstantBuffer(RendererBindingsCb::material, RHI_Shader_Pixel | RHI_Shader_Compute,                     GetConstantBuffer(RendererConstantBuffer::material));

        // Samplers
        cmd_list->SetSampler(0, GetSampler(RendererSampler::compare_depth));
        cmd_list->SetSampler(1, GetSampler(RendererSampler::point_clamp));
        cmd_list->SetSampler(2, GetSampler(RendererSampler::point_wrap));
        cmd_list->SetSampler(3, GetSampler(RendererSampler::bilinear_clamp));
        cmd_list->SetSampler(4, GetSampler(RendererSampler::bilinear_wrap));
        cmd_list->SetSampler(5, GetSampler(RendererSampler::trilinear_clamp));
        cmd_list->SetSampler(6, GetSampler(RendererSampler::anisotropic_wrap));

        // Textures
        cmd_list->SetTexture(RendererBindingsSrv::noise_normal, GetStandardTexture(RendererStandardTexture::noise_normal));
        cmd_list->SetTexture(RendererBindingsSrv::noise_blue, GetStandardTexture(RendererStandardTexture::noise_blue));
    }

    void Renderer::Pass_Main(RHI_CommandList* cmd_list)
    {
        // Validate cmd list
        SP_ASSERT(cmd_list != nullptr);
        SP_ASSERT(cmd_list->GetState() == RHI_CommandListState::Recording);

        SP_PROFILE_FUNCTION();

        // Acquire render targets
        RHI_Texture* rt1       = GetRenderTarget(RendererTexture::frame_render).get();
        RHI_Texture* rt2       = GetRenderTarget(RendererTexture::frame_render_2).get();
        RHI_Texture* rt_output = GetRenderTarget(RendererTexture::frame_output).get();

        // Update frame constant buffer
        UpdateConstantBufferFrame(cmd_list);

        if (shared_ptr<Camera> camera = GetCamera())
        { 
            // If there are no entities, clear to the camera's color
            if (GetEntities()[RendererEntity::geometry_opaque].empty() && GetEntities()[RendererEntity::geometry_transparent].empty() && GetEntities()[RendererEntity::light].empty())
            {
                GetCmdList()->ClearRenderTarget(rt_output, 0, 0, false, camera->GetClearColor());
            }
            else // Render frame
            {
                // Generate brdf specular lut
                if (!m_brdf_specular_lut_rendered)
                {
                    Pass_BrdfSpecularLut(cmd_list);
                    m_brdf_specular_lut_rendered = true;
                }

                // Determine if a transparent pass is required
                const bool do_transparent_pass = !GetEntities()[RendererEntity::geometry_transparent].empty();

                // Shadow maps
                {
                    Pass_ShadowMaps(cmd_list, false);
                    if (do_transparent_pass)
                    {
                        Pass_ShadowMaps(cmd_list, true);
                    }
                }

                Pass_ReflectionProbes(cmd_list);

                // Opaque
                {
                    bool is_transparent_pass = false;

                    Pass_Depth_Prepass(cmd_list);
                    Pass_GBuffer(cmd_list, is_transparent_pass);
                    Pass_Ssgi(cmd_list);
                    Pass_Ssr(cmd_list, rt1);
                    Pass_Light(cmd_list, is_transparent_pass);                  // compute diffuse and specular buffers
                    Pass_Light_Composition(cmd_list, rt1, is_transparent_pass); // compose diffuse, specular, ssgi, volumetric etc.
                    Pass_Light_ImageBased(cmd_list, rt1, is_transparent_pass);  // apply IBL and SSR
                }

                // Transparent
                if (do_transparent_pass)
                {
                    // Blit the frame so that refraction can sample from it
                    cmd_list->Blit(rt1, rt2, true);

                    // Generate frame mips so that the reflections can simulate roughness
                    Pass_Ffx_Spd(cmd_list, rt2);

                    // Blur the smaller mips to reduce blockiness/flickering
                    for (uint32_t i = 1; i < rt2->GetMipCount(); i++)
                    {
                        const bool depth_aware = false;
                        const float radius     = 1.0f;
                        const float sigma      = 12.0f;
                        Pass_Blur_Gaussian(cmd_list, rt2, depth_aware, radius, sigma, i);
                    }

                    bool is_transparent_pass = true;

                    Pass_GBuffer(cmd_list, is_transparent_pass);
                    Pass_Light(cmd_list, is_transparent_pass);
                    Pass_Light_Composition(cmd_list, rt1, is_transparent_pass);
                    Pass_Light_ImageBased(cmd_list, rt1, is_transparent_pass);
                }

                Pass_PostProcess(cmd_list);
            }

            // Editor related stuff - Passes that render on top of each other
            Pass_DebugMeshes(cmd_list, rt_output);
            Pass_Icons(cmd_list, rt_output);
            Pass_PeformanceMetrics(cmd_list, rt_output);
        }
        else
        {
            // If there is no camera, clear to black and and render the performance metrics
            GetCmdList()->ClearRenderTarget(rt_output, 0, 0, false, Color::standard_black);
            Pass_PeformanceMetrics(cmd_list, rt_output);
        }

        // No further rendering is done on this render target, which is the final output.
        // However, ImGui will display it within the viewport, so the appropriate layout has to be set.
        rt_output->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list);
    }

    void Renderer::Pass_ShadowMaps(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        // All objects are rendered from the lights point of view.
        // Opaque objects write their depth information to a depth buffer, using just a vertex shader.
        // Transparent objects read the opaque depth but don't write their own, instead, they write their color information using a pixel shader.

        // Acquire shaders
        RHI_Shader* shader_v = GetShader(RendererShader::depth_light_V).get();
        RHI_Shader* shader_p = GetShader(RendererShader::depth_light_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Get entities
        vector<shared_ptr<Entity>>& entities = m_renderables[is_transparent_pass ? RendererEntity::geometry_transparent : RendererEntity::geometry_opaque];
        if (entities.empty())
            return;

        cmd_list->BeginTimeblock(is_transparent_pass ? "shadow_maps_color" : "shadow_maps_depth");

        // Go through all of the lights
        const auto& entities_light = GetEntities()[RendererEntity::light];
        for (uint32_t light_index = 0; light_index < entities_light.size(); light_index++)
        {
            shared_ptr<Light> light = entities_light[light_index]->GetComponent<Light>();

            // Can happen when loading a new scene and the lights get deleted
            if (!light)
                continue;

            // Skip lights which don't cast shadows or have an intensity of zero
            if (!light->GetShadowsEnabled() || light->GetIntensityForShader(GetCamera().get()) == 0.0f)
                continue;

            // Skip lights that don't cast transparent shadows (if this is a transparent pass)
            if (is_transparent_pass && !light->GetShadowsTransparentEnabled())
                continue;

            // Acquire light's shadow maps
            RHI_Texture* tex_depth = light->GetDepthTexture();
            RHI_Texture* tex_color = light->GetColorTexture();
            if (!tex_depth)
                continue;

            // Define pipeline state
            static RHI_PipelineState pso;
            pso.shader_vertex                   = shader_v;
            pso.shader_pixel                    = is_transparent_pass ? shader_p : nullptr;
            pso.blend_state                     = is_transparent_pass ? GetBlendState(RendererBlendState::alpha).get() : GetBlendState(RendererBlendState::disabled).get();
            pso.depth_stencil_state             = is_transparent_pass ? GetDepthStencilState(RendererDepthStencilState::r_off).get() : GetDepthStencilState(RendererDepthStencilState::rw_off).get();
            pso.render_target_color_textures[0] = tex_color; // always bind so we can clear to white (in case there are no transparent objects)
            pso.render_target_depth_texture     = tex_depth;
            pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;

            for (uint32_t array_index = 0; array_index < tex_depth->GetArrayLength(); array_index++)
            {
                // Set render target texture array index
                pso.render_target_color_texture_array_index         = array_index;
                pso.render_target_depth_stencil_texture_array_index = array_index;

                // Set clear values
                pso.clear_color[0] = Color::standard_white;
                pso.clear_depth    = is_transparent_pass ? rhi_depth_load : 0.0f; // reverse-z

                const Matrix& view_projection = light->GetViewMatrix(array_index) * light->GetProjectionMatrix(array_index);

                // Set appropriate rasterizer state
                if (light->GetLightType() == LightType::Directional)
                {
                    // "Pancaking" - https://www.gamedev.net/forums/topic/639036-shadow-mapping-and-high-up-objects/
                    // It's basically a way to capture the silhouettes of potential shadow casters behind the light's view point.
                    // Of course we also have to make sure that the light doesn't cull them in the first place (this is done automatically by the light)
                    pso.rasterizer_state = GetRasterizerState(RendererRasterizerState::light_directional).get();
                }
                else
                {
                    pso.rasterizer_state = GetRasterizerState(RendererRasterizerState::light_point_spot).get();
                }

                // Set pipeline state
                cmd_list->SetPipelineState(pso);

                // State tracking
                bool render_pass_active    = false;
                uint64_t m_set_material_id = 0;

                for (shared_ptr<Entity> entity : entities)
                {
                    // Acquire renderable component
                    shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>();
                    if (!renderable)
                        continue;

                    // Skip meshes that don't cast shadows
                    if (!renderable->GetCastShadows())
                        continue;

                    // Acquire geometry
                    shared_ptr<Mesh> mesh = renderable->GetMesh();
                    if (!mesh || !mesh->GetVertexBuffer() || !mesh->GetIndexBuffer())
                        continue;

                    // Acquire material
                    Material* material = renderable->GetMaterial();
                    if (!material)
                        continue;

                    // Skip objects outside of the view frustum
                    if (!light->IsInViewFrustum(renderable, array_index))
                        continue;

                    if (!render_pass_active)
                    {
                        cmd_list->BeginRenderPass();
                        render_pass_active = true;
                    }

                    // Bind material (only for transparents)
                    if (is_transparent_pass && m_set_material_id != material->GetObjectId())
                    {
                        // Bind material textures
                        RHI_Texture* tex_albedo = material->GetTexture(MaterialTexture::Color);
                        cmd_list->SetTexture(RendererBindingsSrv::tex, tex_albedo ? tex_albedo : GetStandardTexture(RendererStandardTexture::white).get());

                        // Set uber buffer with material properties
                        m_cb_uber_cpu.mat_color = Vector4(
                            material->GetProperty(MaterialProperty::ColorR),
                            material->GetProperty(MaterialProperty::ColorG),
                            material->GetProperty(MaterialProperty::ColorB),
                            material->GetProperty(MaterialProperty::ColorA)
                        );

                        m_cb_uber_cpu.mat_tiling_uv = Vector2(
                            material->GetProperty(MaterialProperty::UvTilingX),
                            material->GetProperty(MaterialProperty::UvTilingY)
                        );

                        m_cb_uber_cpu.mat_offset_uv = Vector2(
                            material->GetProperty(MaterialProperty::UvOffsetX),
                            material->GetProperty(MaterialProperty::UvOffsetY)
                        );

                        m_set_material_id = material->GetObjectId();
                    }

                    // Bind geometry
                    cmd_list->SetBufferIndex(mesh->GetIndexBuffer());
                    cmd_list->SetBufferVertex(mesh->GetVertexBuffer());

                    // Set uber buffer with cascade transform
                    m_cb_uber_cpu.transform = entity->GetTransform()->GetMatrix() * view_projection;
                    UpdateConstantBufferUber(cmd_list);

                    cmd_list->DrawIndexed(renderable->GetIndexCount(), renderable->GetIndexOffset(), renderable->GetVertexOffset());
                }

                if (render_pass_active)
                {
                    cmd_list->EndRenderPass();
                }
            }
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_ReflectionProbes(RHI_CommandList* cmd_list)
    {
        // Acquire shaders
        RHI_Shader* shader_v = GetShader(RendererShader::reflection_probe_v).get();
        RHI_Shader* shader_p = GetShader(RendererShader::reflection_probe_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Acquire reflections probes
        const vector<shared_ptr<Entity>>& probes = m_renderables[RendererEntity::reflection_probe];
        if (probes.empty())
            return;

        // Acquire renderables
        const vector<shared_ptr<Entity>>& renderables = m_renderables[RendererEntity::geometry_opaque];
        if (renderables.empty())
            return;

        // Acquire lights
        const vector<shared_ptr<Entity>>& lights = m_renderables[RendererEntity::light];
        if (lights.empty())
            return;

        cmd_list->BeginTimeblock("reflection_probes");

        // For each reflection probe
        for (uint32_t probe_index = 0; probe_index < static_cast<uint32_t>(probes.size()); probe_index++)
        {
            shared_ptr<ReflectionProbe> probe = probes[probe_index]->GetComponent<ReflectionProbe>();
            if (!probe || !probe->GetNeedsToUpdate())
                continue;

            // Define pipeline state
            static RHI_PipelineState pso;
            pso.shader_vertex                   = shader_v;
            pso.shader_pixel                    = shader_p;
            pso.rasterizer_state                = GetRasterizerState(RendererRasterizerState::cull_back_solid).get();
            pso.blend_state                     = GetBlendState(RendererBlendState::additive).get();
            pso.depth_stencil_state             = GetDepthStencilState(RendererDepthStencilState::rw_off).get();
            pso.render_target_color_textures[0] = probe->GetColorTexture();
            pso.render_target_depth_texture     = probe->GetDepthTexture();
            pso.clear_color[0]                  = Color::standard_black;
            pso.clear_depth                     = 0.0f; // reverse-z
            pso.clear_stencil                   = rhi_stencil_dont_care;
            pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;

            // Update cube faces
            uint32_t index_start = probe->GetUpdateFaceStartIndex();
            uint32_t index_end   = (index_start + probe->GetUpdateFaceCount()) % 7;
            for (uint32_t face_index = index_start; face_index < index_end; face_index++)
            {
                // Set render target texture array index
                pso.render_target_color_texture_array_index = face_index;

                // Set pipeline state
                cmd_list->SetPipelineState(pso);

                // Begin render pass
                cmd_list->BeginRenderPass();

                // Compute view projection matrix
                Matrix view_projection = probe->GetViewMatrix(face_index) * probe->GetProjectionMatrix();

                // For each renderable entity
                for (uint32_t index_renderable = 0; index_renderable < static_cast<uint32_t>(renderables.size()); index_renderable++)
                {
                    shared_ptr<Entity> entity = renderables[index_renderable];

                    // For each light entity
                    for (uint32_t index_light = 0; index_light < static_cast<uint32_t>(lights.size()); index_light++)
                    {
                        if (shared_ptr<Light> light = lights[index_light]->GetComponent<Light>())
                        {
                            if (light->GetIntensityForShader(GetCamera().get()) != 0)
                            {
                                // Get renderable
                                shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>();
                                if (!renderable)
                                    continue;

                                // Get material
                                Material* material = renderable->GetMaterial();
                                if (!material)
                                    continue;

                                // Get geometry
                                shared_ptr<Mesh> mesh = renderable->GetMesh();
                                if (!mesh || !mesh->GetVertexBuffer() || !mesh->GetIndexBuffer())
                                    continue;

                                // Skip objects outside of the view frustum
                                if (!probe->IsInViewFrustum(renderable, face_index))
                                    continue;

                                // Set geometry (will only happen if not already set)
                                cmd_list->SetBufferIndex(mesh->GetIndexBuffer());
                                cmd_list->SetBufferVertex(mesh->GetVertexBuffer());

                                // Bind material textures
                                cmd_list->SetTexture(RendererBindingsSrv::material_albedo,    material->GetTexture(MaterialTexture::Color));
                                cmd_list->SetTexture(RendererBindingsSrv::material_roughness, material->GetTexture(MaterialTexture::Metallness));
                                cmd_list->SetTexture(RendererBindingsSrv::material_metallic,  material->GetTexture(MaterialTexture::Metallness));

                                // Set uber buffer with material properties
                                m_cb_uber_cpu.mat_color = Vector4(
                                    material->GetProperty(MaterialProperty::ColorR),
                                    material->GetProperty(MaterialProperty::ColorG),
                                    material->GetProperty(MaterialProperty::ColorB),
                                    material->GetProperty(MaterialProperty::ColorA)
                                );
                                m_cb_uber_cpu.mat_textures = 0;
                                m_cb_uber_cpu.mat_textures |= material->HasTexture(MaterialTexture::Color)      ? (1U << 2) : 0;
                                m_cb_uber_cpu.mat_textures |= material->HasTexture(MaterialTexture::Roughness)  ? (1U << 3) : 0;
                                m_cb_uber_cpu.mat_textures |= material->HasTexture(MaterialTexture::Metallness) ? (1U << 4) : 0;

                                // Set uber buffer with cascade transform
                                m_cb_uber_cpu.transform = entity->GetTransform()->GetMatrix() * view_projection;
                                UpdateConstantBufferUber(cmd_list);

                                // Update light buffer
                                UpdateConstantBufferLight(cmd_list, light, RHI_Shader_Pixel);

                                cmd_list->DrawIndexed(renderable->GetIndexCount(), renderable->GetIndexOffset(), renderable->GetVertexOffset());
                            }
                        }
                    }
                }
                cmd_list->EndRenderPass();
            }
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Depth_Prepass(RHI_CommandList* cmd_list)
    {
        if (!GetOption<bool>(RendererOption::DepthPrepass))
            return;

        // Acquire shaders
        RHI_Shader* shader_v = GetShader(RendererShader::depth_prepass_v).get();
        RHI_Shader* shader_p = GetShader(RendererShader::depth_prepass_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        cmd_list->BeginTimeblock("depth_prepass");

        RHI_Texture* tex_depth = GetRenderTarget(RendererTexture::gbuffer_depth).get();
        const vector<shared_ptr<Entity>> entities = m_renderables[RendererEntity::geometry_opaque];

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_vertex               = shader_v;
        pso.shader_pixel                = shader_p; // alpha testing
        pso.rasterizer_state            = GetRasterizerState(RendererRasterizerState::cull_back_solid).get();
        pso.blend_state                 = GetBlendState(RendererBlendState::disabled).get();
        pso.depth_stencil_state         = GetDepthStencilState(RendererDepthStencilState::rw_off).get();
        pso.render_target_depth_texture = tex_depth;
        pso.clear_depth                 = 0.0f; // reverse-z
        pso.primitive_topology          = RHI_PrimitiveTopology_Mode::TriangleList;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Render
        cmd_list->BeginRenderPass();
        { 
            // Variables that help reduce state changes
            uint64_t currently_bound_geometry = 0;
            
            // Draw opaque
            for (shared_ptr<Entity> entity : entities)
            {
                // Get renderable
                shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>();
                if (!renderable)
                    continue;

                // Get material
                Material* material = renderable->GetMaterial();
                if (!material)
                    continue;

                // Get geometry
                shared_ptr<Mesh> mesh = renderable->GetMesh();
                if (!mesh || !mesh->GetVertexBuffer() || !mesh->GetIndexBuffer())
                    continue;

                // Get transform
                shared_ptr<Transform> transform = entity->GetTransform();
                if (!transform)
                    continue;

                // Skip objects outside of the view frustum
                if (!GetCamera()->IsInViewFrustum(renderable))
                    continue;
            
                // Bind geometry
                if (currently_bound_geometry != mesh->GetObjectId())
                {
                    cmd_list->SetBufferIndex(mesh->GetIndexBuffer());
                    cmd_list->SetBufferVertex(mesh->GetVertexBuffer());
                    currently_bound_geometry = mesh->GetObjectId();
                }

                // Bind alpha testing textures
                cmd_list->SetTexture(RendererBindingsSrv::material_albedo,  material->GetTexture(MaterialTexture::Color));
                cmd_list->SetTexture(RendererBindingsSrv::material_mask,    material->GetTexture(MaterialTexture::AlphaMask));

                // Set uber buffer
                m_cb_uber_cpu.transform           = transform->GetMatrix();
                m_cb_uber_cpu.mat_color.w         = material->HasTexture(MaterialTexture::Color) ? 1.0f : 0.0f;
                m_cb_uber_cpu.is_transparent_pass = material->HasTexture(MaterialTexture::AlphaMask);
                UpdateConstantBufferUber(cmd_list);
            
                // Draw
                cmd_list->DrawIndexed(renderable->GetIndexCount(), renderable->GetIndexOffset(), renderable->GetVertexOffset());
            }

            cmd_list->EndRenderPass();
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_GBuffer(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        // Acquire shaders
        RHI_Shader* shader_v = GetShader(RendererShader::gbuffer_v).get();
        RHI_Shader* shader_p = GetShader(RendererShader::gbuffer_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        cmd_list->BeginTimeblock(is_transparent_pass ? "g_buffer_transparent" : "g_buffer");

        // Acquire render targets
        RHI_Texture* tex_albedo            = GetRenderTarget(RendererTexture::gbuffer_albedo).get();
        RHI_Texture* tex_normal            = GetRenderTarget(RendererTexture::gbuffer_normal).get();
        RHI_Texture* tex_material          = GetRenderTarget(RendererTexture::gbuffer_material).get();
        RHI_Texture* tex_velocity          = GetRenderTarget(RendererTexture::gbuffer_velocity).get();
        RHI_Texture* tex_depth             = GetRenderTarget(RendererTexture::gbuffer_depth).get();
        RHI_Texture* tex_fsr2_transparency = GetRenderTarget(RendererTexture::fsr2_mask_transparency).get();

        bool depth_prepass = GetOption<bool>(RendererOption::DepthPrepass);
        bool wireframe     = GetOption<bool>(RendererOption::Debug_Wireframe);

        // Clearing to zero, this will draw the sky
        static Color clear_color = Color(0.0f, 0.0f, 0.0f, 0.0f);

        // Define pipeline state
        RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v;
        pso.shader_pixel                    = shader_p;
        pso.blend_state                     = GetBlendState(RendererBlendState::disabled).get();
        pso.rasterizer_state                = wireframe ? GetRasterizerState(RendererRasterizerState::cull_back_wireframe).get() : GetRasterizerState(RendererRasterizerState::cull_back_solid).get();
        pso.depth_stencil_state             = is_transparent_pass ? GetDepthStencilState(RendererDepthStencilState::rw_w).get() : (depth_prepass ? GetDepthStencilState(RendererDepthStencilState::r_off).get() : GetDepthStencilState(RendererDepthStencilState::rw_off).get());
        pso.render_target_color_textures[0] = tex_albedo;
        pso.clear_color[0]                  = is_transparent_pass ? rhi_color_load : Color::standard_transparent;
        pso.render_target_color_textures[1] = tex_normal;
        pso.clear_color[1]                  = pso.clear_color[0];
        pso.render_target_color_textures[2] = tex_material;
        pso.clear_color[2]                  = pso.clear_color[0];
        pso.render_target_color_textures[3] = tex_velocity;
        pso.clear_color[3]                  = pso.clear_color[0];
        pso.render_target_color_textures[4] = tex_fsr2_transparency;
        pso.clear_color[4]                  = is_transparent_pass ? Color(0.0f, 0.0f, 0.0f, 0.0f) : rhi_color_dont_care;
        pso.render_target_depth_texture     = tex_depth;
        pso.clear_depth                     = (is_transparent_pass || depth_prepass) ? rhi_depth_load : 0.0f; // reverse-z
        pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        uint32_t material_index    = 0;
        uint64_t material_bound_id = 0;
        m_material_instances.fill(nullptr);
        auto& entities = m_renderables[is_transparent_pass ? RendererEntity::geometry_transparent : RendererEntity::geometry_opaque];

        // Render
        cmd_list->BeginRenderPass();
        {
            for (shared_ptr<Entity> entity : entities)
            {
                // Get renderable
                shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>();
                if (!renderable)
                    continue;

                // Get material
                Material* material = renderable->GetMaterial();
                if (!material)
                    continue;


                // Get geometry
                shared_ptr<Mesh> mesh = renderable->GetMesh();
                if (!mesh || !mesh->GetVertexBuffer() || !mesh->GetIndexBuffer())
                    continue;

                // Skip objects outside of the view frustum
                if (!GetCamera()->IsInViewFrustum(renderable))
                    continue;

                // Set geometry (will only happen if not already set)
                cmd_list->SetBufferIndex(mesh->GetIndexBuffer());
                cmd_list->SetBufferVertex(mesh->GetVertexBuffer());

                // Bind material
                const bool firs_run = material_index == 0;
                const bool new_material = material_bound_id != material->GetObjectId();
                if (firs_run || new_material)
                {
                    material_bound_id = material->GetObjectId();

                    // Keep track of used material instances (they get mapped to shaders)
                    if (material_index + 1 < m_material_instances.size())
                    {
                        // Advance index (0 is reserved for the sky)
                        material_index++;

                        // Keep reference
                        m_material_instances[material_index] = material;
                    }
                    else
                    {
                        SP_LOG_ERROR("Material instance array has reached it's maximum capacity of %d elements. Consider increasing the size.", m_max_material_instances);
                    }

                    // Bind material textures
                    cmd_list->SetTexture(RendererBindingsSrv::material_albedo,    material->GetTexture(MaterialTexture::Color));
                    cmd_list->SetTexture(RendererBindingsSrv::material_roughness, material->GetTexture(MaterialTexture::Roughness));
                    cmd_list->SetTexture(RendererBindingsSrv::material_metallic,  material->GetTexture(MaterialTexture::Metallness));
                    cmd_list->SetTexture(RendererBindingsSrv::material_normal,    material->GetTexture(MaterialTexture::Normal));
                    cmd_list->SetTexture(RendererBindingsSrv::material_height,    material->GetTexture(MaterialTexture::Height));
                    cmd_list->SetTexture(RendererBindingsSrv::material_occlusion, material->GetTexture(MaterialTexture::Occlusion));
                    cmd_list->SetTexture(RendererBindingsSrv::material_emission,  material->GetTexture(MaterialTexture::Emission));
                    cmd_list->SetTexture(RendererBindingsSrv::material_mask,      material->GetTexture(MaterialTexture::AlphaMask));

                    // Set uber buffer with material properties
                    m_cb_uber_cpu.mat_id                                 = material_index;
                    m_cb_uber_cpu.mat_color.x                            = material->GetProperty(MaterialProperty::ColorR);
                    m_cb_uber_cpu.mat_color.y                            = material->GetProperty(MaterialProperty::ColorG);
                    m_cb_uber_cpu.mat_color.z                            = material->GetProperty(MaterialProperty::ColorB);
                    m_cb_uber_cpu.mat_color.w                            = material->GetProperty(MaterialProperty::ColorA);
                    m_cb_uber_cpu.mat_tiling_uv.x                        = material->GetProperty(MaterialProperty::UvTilingX);
                    m_cb_uber_cpu.mat_tiling_uv.y                        = material->GetProperty(MaterialProperty::UvTilingY);
                    m_cb_uber_cpu.mat_offset_uv.x                        = material->GetProperty(MaterialProperty::UvOffsetX);
                    m_cb_uber_cpu.mat_offset_uv.y                        = material->GetProperty(MaterialProperty::UvOffsetY);
                    m_cb_uber_cpu.mat_roughness_mul                      = material->GetProperty(MaterialProperty::RoughnessMultiplier);
                    m_cb_uber_cpu.mat_metallic_mul                       = material->GetProperty(MaterialProperty::MetallnessMultiplier);
                    m_cb_uber_cpu.mat_normal_mul                         = material->GetProperty(MaterialProperty::NormalMultiplier);
                    m_cb_uber_cpu.mat_height_mul                         = material->GetProperty(MaterialProperty::HeightMultiplier);
                    m_cb_uber_cpu.mat_single_texture_rougness_metalness  = material->GetProperty(MaterialProperty::SingleTextureRoughnessMetalness);
                    m_cb_uber_cpu.mat_textures                           = 0;
                    m_cb_uber_cpu.mat_textures                          |= material->HasTexture(MaterialTexture::Height)     ? (1U << 0) : 0;
                    m_cb_uber_cpu.mat_textures                          |= material->HasTexture(MaterialTexture::Normal)     ? (1U << 1) : 0;
                    m_cb_uber_cpu.mat_textures                          |= material->HasTexture(MaterialTexture::Color)      ? (1U << 2) : 0;
                    m_cb_uber_cpu.mat_textures                          |= material->HasTexture(MaterialTexture::Roughness)  ? (1U << 3) : 0;
                    m_cb_uber_cpu.mat_textures                          |= material->HasTexture(MaterialTexture::Metallness) ? (1U << 4) : 0;
                    m_cb_uber_cpu.mat_textures                          |= material->HasTexture(MaterialTexture::AlphaMask)  ? (1U << 5) : 0;
                    m_cb_uber_cpu.mat_textures                          |= material->HasTexture(MaterialTexture::Emission)   ? (1U << 6) : 0;
                    m_cb_uber_cpu.mat_textures                          |= material->HasTexture(MaterialTexture::Occlusion)  ? (1U << 7) : 0;
                    m_cb_uber_cpu.is_transparent_pass                    = is_transparent_pass ? 1 : 0;
                }

                // Set uber buffer with entity transform
                if (shared_ptr<Transform> transform = entity->GetTransform())
                {
                    m_cb_uber_cpu.transform          = transform->GetMatrix();
                    m_cb_uber_cpu.transform_previous = transform->GetMatrixPrevious();

                    // Save matrix for velocity computation
                    transform->SetMatrixPrevious(m_cb_uber_cpu.transform);

                    // Update object buffer
                    UpdateConstantBufferUber(cmd_list);
                }

                // Render
                cmd_list->DrawIndexed(renderable->GetIndexCount(), renderable->GetIndexOffset(), renderable->GetVertexOffset());
                Profiler::m_renderer_meshes_rendered++;
            }

            cmd_list->EndRenderPass();
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Lines(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_v = GetShader(RendererShader::line_v).get();
        RHI_Shader* shader_p = GetShader(RendererShader::line_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // reactive mask
        RHI_Texture* tex_reactive_mask  = GetRenderTarget(RendererTexture::fsr2_mask_reactive).get();
        Color clear_color_reactive_mask = Color::standard_black;
        bool clear_reactive_mask        = true;

        cmd_list->BeginTimeblock("lines");

        // Grid
        if (GetOption<bool>(RendererOption::Debug_Grid))
        {
            clear_reactive_mask = false;

            cmd_list->BeginMarker("grid");

            // Define pipeline state
            static RHI_PipelineState pso;
            pso.shader_vertex                   = shader_v;
            pso.shader_pixel                    = shader_p;
            pso.rasterizer_state                = GetRasterizerState(RendererRasterizerState::cull_back_wireframe).get();
            pso.blend_state                     = GetBlendState(RendererBlendState::alpha).get();
            pso.depth_stencil_state             = GetDepthStencilState(RendererDepthStencilState::r_off).get();
            pso.render_target_color_textures[0] = tex_out;
            pso.render_target_color_textures[1] = tex_reactive_mask;
            pso.clear_color[1]                  = clear_color_reactive_mask;
            pso.render_target_depth_texture     = GetRenderTarget(RendererTexture::gbuffer_depth).get();
            pso.primitive_topology              = RHI_PrimitiveTopology_Mode::LineList;

            // Set pipeline state
            cmd_list->SetPipelineState(pso);

            // Render
            cmd_list->BeginRenderPass();
            {
                // Set uber buffer
                m_cb_uber_cpu.resolution_rt = GetResolutionRender();
                if (GetCamera())
                {
                    m_cb_uber_cpu.transform = m_world_grid->ComputeWorldMatrix(GetCamera()->GetTransform()) * m_cb_frame_cpu.view_projection_unjittered;
                }
                UpdateConstantBufferUber(cmd_list);

                cmd_list->SetBufferVertex(m_world_grid->GetVertexBuffer().get());
                cmd_list->Draw(m_world_grid->GetVertexCount());
            }
            cmd_list->EndRenderPass();

            cmd_list->EndMarker();
        }

        // Draw lines
        const bool draw_lines_depth_off = m_lines_index_depth_off != numeric_limits<uint32_t>::max();
        const bool draw_lines_depth_on  = m_lines_index_depth_on > ((m_line_vertices.size() / 2) - 1);
        if (draw_lines_depth_off || draw_lines_depth_on)
        {
            // Grow vertex buffer (if needed)
            uint32_t vertex_count = static_cast<uint32_t>(m_line_vertices.size());
            if (vertex_count > m_vertex_buffer_lines->GetVertexCount())
            {
                m_vertex_buffer_lines->CreateDynamic<RHI_Vertex_PosCol>(vertex_count);
            }

            // If the vertex count is 0, the vertex buffer will be uninitialised.
            if (vertex_count != 0)
            {
                // Update vertex buffer
                RHI_Vertex_PosCol* buffer = static_cast<RHI_Vertex_PosCol*>(m_vertex_buffer_lines->Map());
                copy(m_line_vertices.begin(), m_line_vertices.end(), buffer);
                m_vertex_buffer_lines->Unmap();

                // Define pipeline state
                static RHI_PipelineState pso;
                pso.shader_vertex                   = shader_v;
                pso.shader_pixel                    = shader_p;
                pso.rasterizer_state                = GetRasterizerState(RendererRasterizerState::cull_back_wireframe).get();
                pso.render_target_color_textures[0] = tex_out;
                pso.render_target_color_textures[1] = tex_reactive_mask;
                pso.clear_color[1]                  = clear_reactive_mask ? clear_color_reactive_mask : rhi_color_load;
                pso.primitive_topology              = RHI_PrimitiveTopology_Mode::LineList;

                // Depth off
                if (draw_lines_depth_off)
                {
                    cmd_list->BeginMarker("depth_off");

                    // Define pipeline state
                    pso.blend_state         = GetBlendState(RendererBlendState::disabled).get();
                    pso.depth_stencil_state = GetDepthStencilState(RendererDepthStencilState::off_off).get();

                    // Set pipeline state
                    cmd_list->SetPipelineState(pso);

                    // Render
                    cmd_list->BeginRenderPass();
                    {
                        cmd_list->SetBufferVertex(m_vertex_buffer_lines.get());
                        cmd_list->Draw(m_lines_index_depth_off + 1); 
                    }
                    cmd_list->EndRenderPass();

                    cmd_list->EndMarker();
                }

                // Depth on
                if (m_lines_index_depth_on > (vertex_count / 2) - 1)
                {
                    cmd_list->BeginMarker("depth_on");

                    // Define pipeline state
                    pso.blend_state                 = GetBlendState(RendererBlendState::alpha).get();
                    pso.depth_stencil_state         = GetDepthStencilState(RendererDepthStencilState::r_off).get();
                    pso.render_target_depth_texture = GetRenderTarget(RendererTexture::gbuffer_depth).get();

                    // Set pipeline state
                    cmd_list->SetPipelineState(pso);

                    // Render
                    cmd_list->BeginRenderPass();
                    {
                        cmd_list->SetBufferVertex(m_vertex_buffer_lines.get());
                        cmd_list->Draw((m_lines_index_depth_on - (vertex_count / 2)) + 1, vertex_count / 2);
                    }
                    cmd_list->EndRenderPass();

                    cmd_list->EndMarker();
                }
            }
        }

        if (clear_reactive_mask)
        {
            cmd_list->ClearRenderTarget(tex_reactive_mask, 0, 0, false, clear_color_reactive_mask);
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Ssgi(RHI_CommandList* cmd_list)
    {
        if (!GetOption<bool>(RendererOption::Ssgi))
            return;

        // Acquire shaders
        RHI_Shader* shader_c = GetShader(RendererShader::ssgi_c).get();
        if (!shader_c->IsCompiled())
            return;

        // Acquire render targets
        RHI_Texture* tex_ssgi = GetRenderTarget(RendererTexture::ssgi).get();

        cmd_list->BeginTimeblock("ssgi");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Set uber buffer
        m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_ssgi->GetWidth()), static_cast<float>(tex_ssgi->GetHeight()));
        UpdateConstantBufferUber(cmd_list);

        // Set textures
        cmd_list->SetTexture(RendererBindingsUav::tex,            tex_ssgi);
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_albedo, GetRenderTarget(RendererTexture::gbuffer_albedo));
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal, GetRenderTarget(RendererTexture::gbuffer_normal));
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth,  GetRenderTarget(RendererTexture::gbuffer_depth));
        cmd_list->SetTexture(RendererBindingsSrv::light_diffuse,  GetRenderTarget(RendererTexture::light_diffuse));

        // Render
        cmd_list->Dispatch(thread_group_count_x(tex_ssgi), thread_group_count_y(tex_ssgi));

        // Blur
        const bool depth_aware = true;
        const float radius     = 12.0f;
        const float sigma      = 12.0f;
        Pass_Blur_Gaussian(cmd_list, tex_ssgi, depth_aware, radius, sigma);

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Ssr(RHI_CommandList* cmd_list, RHI_Texture* tex_in)
    {
        if (!GetOption<bool>(RendererOption::ScreenSpaceReflections))
            return;

        // Acquire shaders
        RHI_Shader* shader_c = GetShader(RendererShader::ssr_c).get();
        if (!shader_c->IsCompiled())
            return;

        // Acquire render targets
        RHI_Texture* tex_ssr = GetRenderTarget(RendererTexture::ssr).get();

        cmd_list->BeginTimeblock("ssr");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Set uber buffer
        m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_ssr->GetWidth()), static_cast<float>(tex_ssr->GetHeight()));
        UpdateConstantBufferUber(cmd_list);

        // Set textures
        cmd_list->SetTexture(RendererBindingsUav::tex, tex_ssr);    // write to that
        cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);     // reflect from that
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_albedo,   GetRenderTarget(RendererTexture::gbuffer_albedo));
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal,   GetRenderTarget(RendererTexture::gbuffer_normal));
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth,    GetRenderTarget(RendererTexture::gbuffer_depth));
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_material, GetRenderTarget(RendererTexture::gbuffer_material));
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_velocity, GetRenderTarget(RendererTexture::gbuffer_velocity));

        // Render
        cmd_list->Dispatch(thread_group_count_x(tex_ssr), thread_group_count_y(tex_ssr));

        // Generate frame mips so that we can simulate roughness
        Pass_Ffx_Spd(cmd_list, tex_ssr);

        // Blur the smaller mips to reduce blockiness/flickering
        for (uint32_t i = 1; i < tex_ssr->GetMipCount(); i++)
        {
            const bool depth_aware = true;
            const float radius     = 5.0f;
            const float sigma      = 2.0f;
            Pass_Blur_Gaussian(cmd_list, tex_ssr, depth_aware, radius, sigma, i);
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Light(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        // Acquire shaders
        RHI_Shader* shader_c = GetShader(RendererShader::light_c).get();
        if (!shader_c->IsCompiled())
            return;

        // Acquire lights
        const vector<shared_ptr<Entity>>& entities = m_renderables[RendererEntity::light];
        if (entities.empty())
            return;

        cmd_list->BeginTimeblock(is_transparent_pass ? "light_transparent" : "light");

        // Acquire render targets
        RHI_Texture* tex_diffuse    = is_transparent_pass ? GetRenderTarget(RendererTexture::light_diffuse_transparent).get()  : GetRenderTarget(RendererTexture::light_diffuse).get();
        RHI_Texture* tex_specular   = is_transparent_pass ? GetRenderTarget(RendererTexture::light_specular_transparent).get() : GetRenderTarget(RendererTexture::light_specular).get();
        RHI_Texture* tex_volumetric = GetRenderTarget(RendererTexture::light_volumetric).get();

        // Clear render targets
        cmd_list->ClearRenderTarget(tex_diffuse,    0, 0, true, Color::standard_black);
        cmd_list->ClearRenderTarget(tex_specular,   0, 0, true, Color::standard_black);
        cmd_list->ClearRenderTarget(tex_volumetric, 0, 0, true, Color::standard_black);

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Iterate through all the light entities
        for (shared_ptr<Entity> entity : entities)
        {
            if (shared_ptr<Light> light = entity->GetComponent<Light>())
            {
                // Do the lighting even when intensity is zero, since we can have emissive lighting.
                cmd_list->SetTexture(RendererBindingsUav::tex,              tex_diffuse);
                cmd_list->SetTexture(RendererBindingsUav::tex2,             tex_specular);
                cmd_list->SetTexture(RendererBindingsUav::tex3,             tex_volumetric);
                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_albedo,   GetRenderTarget(RendererTexture::gbuffer_albedo));
                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal,   GetRenderTarget(RendererTexture::gbuffer_normal));
                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_material, GetRenderTarget(RendererTexture::gbuffer_material));
                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth,    GetRenderTarget(RendererTexture::gbuffer_depth));
                cmd_list->SetTexture(RendererBindingsSrv::ssgi,             GetRenderTarget(RendererTexture::ssgi));
                
                // Set shadow maps
                {
                    // We always bind all the shadow maps, regardless of the light type or if shadows are enabled.
                    // This is because we are using an uber shader and APIs like Vulkan, expect all texture slots to be bound with something.

                    RHI_Texture* tex_depth = light->GetDepthTexture();
                    RHI_Texture* tex_color = light->GetShadowsTransparentEnabled() ? light->GetColorTexture() : GetStandardTexture(RendererStandardTexture::white).get();

                    if (light->GetLightType() == LightType::Directional)
                    {
                        cmd_list->SetTexture(RendererBindingsSrv::light_directional_depth, tex_depth);
                        cmd_list->SetTexture(RendererBindingsSrv::light_directional_color, tex_color);
                    }
                    else if (light->GetLightType() == LightType::Point)
                    {
                        cmd_list->SetTexture(RendererBindingsSrv::light_point_depth, tex_depth);
                        cmd_list->SetTexture(RendererBindingsSrv::light_point_color, tex_color);
                    }
                    else if (light->GetLightType() == LightType::Spot)
                    {
                        cmd_list->SetTexture(RendererBindingsSrv::light_spot_depth, tex_depth);
                        cmd_list->SetTexture(RendererBindingsSrv::light_spot_color, tex_color);
                    }
                }
                
                // Update materials structured buffer (light pass will access it using material IDs)
                UpdateConstantBufferMaterial(cmd_list);
                
                // Update light buffer
                UpdateConstantBufferLight(cmd_list, light, RHI_Shader_Compute);
                
                // Set uber buffer
                m_cb_uber_cpu.resolution_rt       = Vector2(static_cast<float>(tex_diffuse->GetWidth()), static_cast<float>(tex_diffuse->GetHeight()));
                m_cb_uber_cpu.is_transparent_pass = is_transparent_pass;
                UpdateConstantBufferUber(cmd_list);
                
                cmd_list->Dispatch(thread_group_count_x(tex_diffuse), thread_group_count_y(tex_diffuse));
            }
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Light_Composition(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass)
    {
        // Acquire shaders
        RHI_Shader* shader_c = GetShader(RendererShader::light_composition_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock(is_transparent_pass ? "light_composition_transparent" : "light_composition");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Set uber buffer
        m_cb_uber_cpu.resolution_rt       = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        m_cb_uber_cpu.is_transparent_pass = is_transparent_pass;
        UpdateConstantBufferUber(cmd_list);

        // Update light buffer with the directional light
        {
            const vector<shared_ptr<Entity>>& entities = m_renderables[RendererEntity::light];
            for (shared_ptr<Entity> entity : entities)
            {
                if (entity->GetComponent<Light>()->GetLightType() == LightType::Directional)
                {
                    UpdateConstantBufferLight(cmd_list, entity->GetComponent<Light>(), RHI_Shader_Compute);
                    break;
                }
            }
        }

        // Set textures
        cmd_list->SetTexture(RendererBindingsUav::tex,              tex_out);
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_albedo,   GetRenderTarget(RendererTexture::gbuffer_albedo));
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_material, GetRenderTarget(RendererTexture::gbuffer_material));
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal,   GetRenderTarget(RendererTexture::gbuffer_normal));
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth,    GetRenderTarget(RendererTexture::gbuffer_depth));
        cmd_list->SetTexture(RendererBindingsSrv::light_diffuse,    is_transparent_pass ? GetRenderTarget(RendererTexture::light_diffuse_transparent).get()  : GetRenderTarget(RendererTexture::light_diffuse).get());
        cmd_list->SetTexture(RendererBindingsSrv::light_specular,   is_transparent_pass ? GetRenderTarget(RendererTexture::light_specular_transparent).get() : GetRenderTarget(RendererTexture::light_specular).get());
        cmd_list->SetTexture(RendererBindingsSrv::light_volumetric, GetRenderTarget(RendererTexture::light_volumetric));
        cmd_list->SetTexture(RendererBindingsSrv::frame,            GetRenderTarget(RendererTexture::frame_render_2)); // refraction
        cmd_list->SetTexture(RendererBindingsSrv::ssgi,             GetRenderTarget(RendererTexture::ssgi));
        cmd_list->SetTexture(RendererBindingsSrv::environment,      GetEnvironmentTexture());

        // Render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Light_ImageBased(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass)
    {
        // Acquire shaders
        RHI_Shader* shader_v = GetShader(RendererShader::fullscreen_triangle_v).get();
        RHI_Shader* shader_p = GetShader(RendererShader::light_image_based_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        cmd_list->BeginTimeblock(is_transparent_pass ? "light_image_based_transparent" : "light_image_based");

        // Get reflection probe entities
        const vector<shared_ptr<Entity>>& probes = m_renderables[RendererEntity::reflection_probe];

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v;
        pso.shader_pixel                    = shader_p;
        pso.rasterizer_state                = GetRasterizerState(RendererRasterizerState::cull_back_solid).get();
        pso.depth_stencil_state             = GetDepthStencilState(RendererDepthStencilState::off_off).get();
        pso.blend_state                     = GetBlendState(RendererBlendState::additive).get();
        pso.render_target_color_textures[0] = tex_out;
        pso.clear_color[0]                  = rhi_color_load;
        pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;
        pso.can_use_vertex_index_buffers    = false;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Set textures
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_albedo,   GetRenderTarget(RendererTexture::gbuffer_albedo));
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal,   GetRenderTarget(RendererTexture::gbuffer_normal));
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_material, GetRenderTarget(RendererTexture::gbuffer_material));
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth,    GetRenderTarget(RendererTexture::gbuffer_depth));
        cmd_list->SetTexture(RendererBindingsSrv::ssgi,             GetRenderTarget(RendererTexture::ssgi));
        cmd_list->SetTexture(RendererBindingsSrv::ssr,              GetRenderTarget(RendererTexture::ssr));
        cmd_list->SetTexture(RendererBindingsSrv::lutIbl,           GetRenderTarget(RendererTexture::brdf_specular_lut));
        cmd_list->SetTexture(RendererBindingsSrv::environment,      GetEnvironmentTexture());

        // Set probe textures and data
        if (!probes.empty())
        {
            shared_ptr<ReflectionProbe> probe = probes[0]->GetComponent<ReflectionProbe>();

            cmd_list->SetTexture(RendererBindingsSrv::reflection_probe, probe->GetColorTexture());
            m_cb_uber_cpu.extents  = probe->GetExtents();
            m_cb_uber_cpu.position = probe->GetTransform()->GetPosition();
        }

        // Set uber buffer
        m_cb_uber_cpu.resolution_rt               = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        m_cb_uber_cpu.is_transparent_pass         = is_transparent_pass;
        m_cb_uber_cpu.reflection_proble_available = !probes.empty();
        UpdateConstantBufferUber(cmd_list);

        // Update light buffer with the directional light
        {
            const vector<shared_ptr<Entity>>& entities = m_renderables[RendererEntity::light];
            for (shared_ptr<Entity> entity : entities)
            {
                if (entity->GetComponent<Light>()->GetLightType() == LightType::Directional)
                {
                    UpdateConstantBufferLight(cmd_list, entity->GetComponent<Light>(), RHI_Shader_Pixel);
                    break;
                }
            }
        }

        // Render
        cmd_list->BeginRenderPass();
        {
            cmd_list->Draw(3, 0);
            cmd_list->EndRenderPass();
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Blur_Gaussian(RHI_CommandList* cmd_list, RHI_Texture* tex_in, const bool depth_aware, const float radius, const float sigma, const uint32_t mip /*= all_mips*/)
    {
        // Acquire shaders
        RHI_Shader* shader_c = GetShader(depth_aware ? RendererShader::blur_gaussian_bilaterial_c : RendererShader::blur_gaussian_c).get();
        if (!shader_c->IsCompiled())
            return;

        const float pixel_stride = 1.0f;
        const bool mip_requested = mip != rhi_all_mips;
        const uint32_t mip_range = mip_requested ? 1 : 0;

        // If we need to blur a specific mip, ensure that the texture has per mip views
        if (mip_requested)
        {
            SP_ASSERT(tex_in->HasPerMipViews());
        }

        // Compute width and height
        const uint32_t width  = mip_requested ? (tex_in->GetWidth()  >> mip) : tex_in->GetWidth();
        const uint32_t height = mip_requested ? (tex_in->GetHeight() >> mip) : tex_in->GetHeight();

        // Acquire render targets
        RHI_Texture* tex_depth  = GetRenderTarget(RendererTexture::gbuffer_depth).get();
        RHI_Texture* tex_normal = GetRenderTarget(RendererTexture::gbuffer_normal).get();
        RHI_Texture* tex_blur   = GetRenderTarget(RendererTexture::blur).get();

        // Ensure that the blur scratch texture is big enough
        SP_ASSERT(tex_blur->GetWidth() >= width && tex_blur->GetHeight() >= height);

        // Compute thread group count
        const uint32_t thread_group_count_x_ = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(width) / m_thread_group_count));
        const uint32_t thread_group_count_y_ = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(height) / m_thread_group_count));

        cmd_list->BeginMarker("blur_gaussian");

        // Horizontal pass
        {
            // Define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_c;

            // Set pipeline state
            cmd_list->SetPipelineState(pso);

            // Set uber buffer
            m_cb_uber_cpu.resolution_rt  = Vector2(static_cast<float>(width), static_cast<float>(height));
            m_cb_uber_cpu.resolution_in  = Vector2(static_cast<float>(width), static_cast<float>(height));
            m_cb_uber_cpu.blur_radius    = radius;
            m_cb_uber_cpu.blur_sigma     = sigma;
            m_cb_uber_cpu.blur_direction = Vector2(pixel_stride, 0.0f);
            UpdateConstantBufferUber(cmd_list);

            // Set textures
            cmd_list->SetTexture(RendererBindingsUav::tex, tex_blur);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in, mip, mip_range);
            if (depth_aware)
            {
                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal, tex_normal);
            }

            // Render
            cmd_list->Dispatch(thread_group_count_x_, thread_group_count_y_);
        }

        // Vertical pass
        {
            // Define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_c;

            // Set pipeline state
            cmd_list->SetPipelineState(pso);

            // Set uber buffer
            m_cb_uber_cpu.resolution_rt  = Vector2(static_cast<float>(tex_blur->GetWidth()), static_cast<float>(tex_blur->GetHeight()));
            m_cb_uber_cpu.blur_direction = Vector2(0.0f, pixel_stride);
            UpdateConstantBufferUber(cmd_list);

            // Set textures
            cmd_list->SetTexture(RendererBindingsUav::tex, tex_in, mip, mip_range);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_blur);
            if (depth_aware)
            {
                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal, tex_normal);
            }

            // Render
            cmd_list->Dispatch(thread_group_count_x_, thread_group_count_y_);
        }

        cmd_list->EndMarker();
    }

    void Renderer::Pass_PostProcess(RHI_CommandList* cmd_list)
    {
        // IN:  Frame_Render, which is and HDR render resolution render target (with a second texture so passes can alternate between them)
        // OUT: Frame_Output, which is and LDR output resolution render target (with a second texture so passes can alternate between them)

        // Acquire render targets (as references so that swapping the pointers around works)
        RHI_Texture* rt_frame_render         = GetRenderTarget(RendererTexture::frame_render).get();   // render res
        RHI_Texture* rt_frame_render_scratch = GetRenderTarget(RendererTexture::frame_render_2).get(); // render res
        RHI_Texture* rt_frame_output         = GetRenderTarget(RendererTexture::frame_output).get();   // output res
        RHI_Texture* rt_frame_output_scratch = GetRenderTarget(RendererTexture::frame_output_2).get(); // output res

        // A bunch of macros which allows us to keep track of which texture is an input/output for each pass.
        bool swap_render = true;
        #define get_render_in  swap_render ? rt_frame_render_scratch : rt_frame_render
        #define get_render_out swap_render ? rt_frame_render : rt_frame_render_scratch
        bool swap_output = true;
        #define get_output_in  swap_output ? rt_frame_output_scratch : rt_frame_output
        #define get_output_out swap_output ? rt_frame_output : rt_frame_output_scratch
 
        cmd_list->BeginMarker("post_proccess");

        // RENDER RESOLUTION
        {
            // Depth of Field
            if (GetOption<bool>(RendererOption::DepthOfField))
            {
                swap_render = !swap_render;
                Pass_DepthOfField(cmd_list, get_render_in, get_render_out);
            }

            // Line rendering (world grid, vectors, debugging etc)
            Pass_Lines(cmd_list, get_render_out);
            Pass_Outline(cmd_list, get_render_out);
        }

        // Determine antialiasing modes
        AntialiasingMode antialiasing = GetOption<AntialiasingMode>(RendererOption::Antialiasing);
        bool taa_enabled              = antialiasing == AntialiasingMode::Taa  || antialiasing == AntialiasingMode::TaaFxaa;
        bool fxaa_enabled             = antialiasing == AntialiasingMode::Fxaa || antialiasing == AntialiasingMode::TaaFxaa;

        // Get upsampling mode
        UpsamplingMode upsampling_mode = GetOption<UpsamplingMode>(RendererOption::Upsampling);

        // RENDER RESOLUTION -> OUTPUT RESOLUTION
        {
            // FSR 2.0 (It can be used both for upsampling and just TAA)
            if (upsampling_mode == UpsamplingMode::FSR2 || taa_enabled)
            {
                swap_render = !swap_render;
                Pass_Ffx_Fsr2(cmd_list, get_render_in, rt_frame_output);
            }
            // Linear
            else if (upsampling_mode == UpsamplingMode::Linear)
            {
                // D3D11 baggage, can't blit to a texture with a different resolution or mip count
                swap_render   = !swap_render;
                bool bilinear = GetResolutionOutput() != GetResolutionRender();
                Pass_Copy(cmd_list, get_render_in, rt_frame_output, bilinear);
            }
        }

        // OUTPUT RESOLUTION
        {
            // Motion Blur
            if (GetOption<bool>(RendererOption::MotionBlur))
            {
                swap_output = !swap_output;
                Pass_MotionBlur(cmd_list, get_output_in, get_output_out);
            }

            // Bloom
            if (GetOption<bool>(RendererOption::Bloom))
            {
                swap_output = !swap_output;
                Pass_Bloom(cmd_list, get_output_in, get_output_out);
            }

            // Tone-Mapping & Gamma Correction
            swap_output = !swap_output;
            Pass_ToneMappingGammaCorrection(cmd_list, get_output_in, get_output_out);

            // Sharpening
            if (GetOption<bool>(RendererOption::Sharpness))
            {
                // FidelityFX FSR 2.0 sharpening overrides FidelityFX CAS
                if (upsampling_mode != UpsamplingMode::FSR2)
                {
                    swap_output = !swap_output;
                    Pass_Ffx_Cas(cmd_list, get_output_in, get_output_out);
                }
            }

            // Debanding
            if (GetOption<bool>(RendererOption::Debanding))
            {
                swap_output = !swap_output;
                Pass_Debanding(cmd_list, get_output_in, get_output_out);
            }

            // FXAA
            if (fxaa_enabled)
            {
                swap_output = !swap_output;
                Pass_Fxaa(cmd_list, get_output_in, get_output_out);
            }

            // Chromatic aberration
            if (GetOption<bool>(RendererOption::ChromaticAberration))
            {
                swap_output = !swap_output;
                Pass_ChromaticAberration(cmd_list, get_output_in, get_output_out);
            }

            // Film grain
            if (GetOption<bool>(RendererOption::FilmGrain))
            {
                swap_output = !swap_output;
                Pass_FilmGrain(cmd_list, get_output_in, get_output_out);
            }
        }

        // If the last written texture is not the output one, then make sure it is.
        if (!swap_output)
        {
            cmd_list->Blit(rt_frame_output_scratch, rt_frame_output, false);
        }

        cmd_list->EndMarker();
    }

    void Renderer::Pass_Bloom(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_luminance        = GetShader(RendererShader::bloom_luminance_c).get();
        RHI_Shader* shader_upsampleBlendMip = GetShader(RendererShader::bloom_upsample_blend_mip_c).get();
        RHI_Shader* shader_blendFrame       = GetShader(RendererShader::bloom_blend_frame_c).get();

        if (!shader_luminance->IsCompiled() || !shader_upsampleBlendMip->IsCompiled() || !shader_blendFrame->IsCompiled())
            return;

        cmd_list->BeginTimeblock("bloom");

        // Acquire render target
        RHI_Texture* tex_bloom = GetRenderTarget(RendererTexture::bloom).get();

        // Luminance
        cmd_list->BeginMarker("luminance");
        {
            // Define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_luminance;

            // Set pipeline state
            cmd_list->SetPipelineState(pso);

            // Set uber buffer
            m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_bloom->GetWidth()), static_cast<float>(tex_bloom->GetHeight()));
            UpdateConstantBufferUber(cmd_list);

            // Set textures
            cmd_list->SetTexture(RendererBindingsUav::tex, tex_bloom);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);

            // Render
            cmd_list->Dispatch(thread_group_count_x(tex_bloom), thread_group_count_y(tex_bloom));
        }
        cmd_list->EndMarker();

        // Generate mips
        Pass_Ffx_Spd(cmd_list, tex_bloom);

        // Starting from the lowest mip, upsample and blend with the higher one
        cmd_list->BeginMarker("upsample_and_blend_with_higher_mip");
        {
            // Define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_upsampleBlendMip;

            // Set pipeline state
            cmd_list->SetPipelineState(pso);

            // Render
            for (int i = static_cast<int>(tex_bloom->GetMipCount() - 1); i > 0; i--)
            {
                int mip_index_small   = i;
                int mip_index_big     = i - 1;
                int mip_width_large   = tex_bloom->GetWidth() >> mip_index_big;
                int mip_height_height = tex_bloom->GetHeight() >> mip_index_big;

                // Set uber buffer
                m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(mip_width_large), static_cast<float>(mip_height_height));
                UpdateConstantBufferUber(cmd_list);

                // Set textures
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_bloom, mip_index_small, 1);
                cmd_list->SetTexture(RendererBindingsUav::tex, tex_bloom, mip_index_big, 1);

                // Blend
                uint32_t thread_group_count_x_ = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(mip_width_large) / m_thread_group_count));
                uint32_t thread_group_count_y_ = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(mip_height_height) / m_thread_group_count));
                cmd_list->Dispatch(thread_group_count_x_, thread_group_count_y_);
            }
        }
        cmd_list->EndMarker();

        // Blend with the frame
        cmd_list->BeginMarker("blend_with_frame");
        {
            // Define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_blendFrame;

            // Set pipeline state
            cmd_list->SetPipelineState(pso);

            // Set uber buffer
            m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateConstantBufferUber(cmd_list);

            // Set textures
            cmd_list->SetTexture(RendererBindingsUav::tex, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->SetTexture(RendererBindingsSrv::tex2, tex_bloom, 0, 1);

            // Render
            cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));
        }
        cmd_list->EndMarker();

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_ToneMappingGammaCorrection(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = GetShader(RendererShader::tonemapping_gamma_correction_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock("tonemapping_gamma_correction");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Set uber buffer
        m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateConstantBufferUber(cmd_list);

        // Set textures
        cmd_list->SetTexture(RendererBindingsUav::tex, tex_out);
        cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);

        // Render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Fxaa(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = GetShader(RendererShader::fxaa_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock("fxaa");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Set uber buffer
        m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateConstantBufferUber(cmd_list);

        // Set textures
        cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
        cmd_list->SetTexture(RendererBindingsUav::tex, tex_out);
        
        // Render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_ChromaticAberration(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = GetShader(RendererShader::chromatic_aberration_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock("chromatic_aberration");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Set uber buffer
        m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateConstantBufferUber(cmd_list);

        // Set textures
        cmd_list->SetTexture(RendererBindingsUav::tex, tex_out);
        cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);

        // Render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_MotionBlur(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = GetShader(RendererShader::motion_blur_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock("motion_blur");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Set uber buffer
        m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateConstantBufferUber(cmd_list);

        // Set textures
        cmd_list->SetTexture(RendererBindingsUav::tex, tex_out);
        cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_velocity, GetRenderTarget(RendererTexture::gbuffer_velocity));
        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth,    GetRenderTarget(RendererTexture::gbuffer_depth));

        // Render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_DepthOfField(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_downsampleCoc = GetShader(RendererShader::dof_downsample_coc_c).get();
        RHI_Shader* shader_bokeh         = GetShader(RendererShader::dof_bokeh_c).get();
        RHI_Shader* shader_tent          = GetShader(RendererShader::dof_tent_c).get();
        RHI_Shader* shader_upsampleBlend = GetShader(RendererShader::dof_upscale_blend_c).get();
        if (!shader_downsampleCoc->IsCompiled() || !shader_bokeh->IsCompiled() || !shader_tent->IsCompiled() || !shader_upsampleBlend->IsCompiled())
            return;

        cmd_list->BeginTimeblock("depth_of_field");

        // Acquire render targets
        RHI_Texture* tex_bokeh_half   = GetRenderTarget(RendererTexture::dof_half).get();
        RHI_Texture* tex_bokeh_half_2 = GetRenderTarget(RendererTexture::dof_half_2).get();
        RHI_Texture* tex_depth        = GetRenderTarget(RendererTexture::gbuffer_depth).get();

        // Downsample and compute circle of confusion
        cmd_list->BeginMarker("circle_of_confusion");
        {
            // Define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_downsampleCoc;

            // Set pipeline state
            cmd_list->SetPipelineState(pso);

            // Set uber buffer
            m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_bokeh_half->GetWidth()), static_cast<float>(tex_bokeh_half->GetHeight()));
            UpdateConstantBufferUber(cmd_list);

            // Set textures
            cmd_list->SetTexture(RendererBindingsUav::tex, tex_bokeh_half);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);

            // Render
            cmd_list->Dispatch(thread_group_count_x(tex_bokeh_half), thread_group_count_y(tex_bokeh_half));
        }
        cmd_list->EndMarker();

        // Bokeh
        cmd_list->BeginMarker("bokeh");
        {
            // Define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_bokeh;

            // Set pipeline state
            cmd_list->SetPipelineState(pso);

            // Set uber buffer
            m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_bokeh_half_2->GetWidth()), static_cast<float>(tex_bokeh_half_2->GetHeight()));
            UpdateConstantBufferUber(cmd_list);

            // Set textures
            cmd_list->SetTexture(RendererBindingsUav::tex, tex_bokeh_half_2);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_bokeh_half);

            // Render
            cmd_list->Dispatch(thread_group_count_x(tex_bokeh_half_2), thread_group_count_y(tex_bokeh_half_2));
        }
        cmd_list->EndMarker();

        // Blur the bokeh using a tent filter
        cmd_list->BeginMarker("tent");
        {
            // Define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute   = shader_tent;

            // Set pipeline state
            cmd_list->SetPipelineState(pso);

            // Set uber buffer
            m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_bokeh_half->GetWidth()), static_cast<float>(tex_bokeh_half->GetHeight()));
            UpdateConstantBufferUber(cmd_list);

            // Set textures
            cmd_list->SetTexture(RendererBindingsUav::tex, tex_bokeh_half);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_bokeh_half_2);

            // Render
            cmd_list->Dispatch(thread_group_count_x(tex_bokeh_half), thread_group_count_y(tex_bokeh_half));
        }
        cmd_list->EndMarker();

        // Upscale & Blend
        cmd_list->BeginMarker("upsample_and_blend_with_frame");
        {
            // Define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_upsampleBlend;

            // Set pipeline state
            cmd_list->SetPipelineState(pso);

            // Set uber buffer
            m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateConstantBufferUber(cmd_list);

            // Set textures
            cmd_list->SetTexture(RendererBindingsUav::tex, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->SetTexture(RendererBindingsSrv::tex2, tex_bokeh_half);

            // Render
            cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));
        }
        cmd_list->EndMarker();

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Debanding(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader = GetShader(RendererShader::debanding_c).get();
        if (!shader->IsCompiled())
            return;

        cmd_list->BeginTimeblock("debanding");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Set uber buffer
        m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateConstantBufferUber(cmd_list);

        // Set textures
        cmd_list->SetTexture(RendererBindingsUav::tex, tex_out);
        cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);

        // Render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_FilmGrain(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = GetShader(RendererShader::film_grain_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock("film_grain");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Render
        // Set uber buffer
        m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateConstantBufferUber(cmd_list);

        // Set textures
        cmd_list->SetTexture(RendererBindingsUav::tex, tex_out);
        cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);

        // Render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Ffx_Cas(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = GetShader(RendererShader::ffx_cas_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock("ffx_cas");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Set uber buffer
        m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateConstantBufferUber(cmd_list);

        // Set textures
        cmd_list->SetTexture(RendererBindingsUav::tex, tex_out);
        cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);

        // Render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Ffx_Spd(RHI_CommandList* cmd_list, RHI_Texture* tex)
    {
        // AMD FidelityFX Single Pass Downsampler.
        // Provides an RDNA™-optimized solution for generating up to 12 MIP levels of a texture.
        // GitHub:        https://github.com/GPUOpen-Effects/FidelityFX-SPD
        // Documentation: https://github.com/GPUOpen-Effects/FidelityFX-SPD/blob/master/docs/FidelityFX_SPD.pdf

        uint32_t output_mip_count = tex->GetMipCount() - 1;
        uint32_t smallest_width   = tex->GetWidth() >> output_mip_count;
        uint32_t smallest_height  = tex->GetWidth() >> output_mip_count;

        // Ensure that the input texture meets the requirements.
        SP_ASSERT(tex->HasPerMipViews());
        SP_ASSERT(output_mip_count <= 12); // As per documentation (page 22)

        // Acquire shader
        RHI_Shader* shader_c = GetShader(RendererShader::ffx_spd_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginMarker("ffx_spd");

        // Define render state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // As per documentation (page 22)
        const uint32_t thread_group_count_x_ = (tex->GetWidth() + 63) >> 6;
        const uint32_t thread_group_count_y_ = (tex->GetHeight() + 63) >> 6;

        // Set uber buffer
        m_cb_uber_cpu.resolution_rt    = Vector2(static_cast<float>(tex->GetWidth()), static_cast<float>(tex->GetHeight()));
        m_cb_uber_cpu.mip_count        = output_mip_count;
        m_cb_uber_cpu.work_group_count = thread_group_count_x_ * thread_group_count_y_;
        UpdateConstantBufferUber(cmd_list);

        // Update counter
        uint32_t counter_value = 0;
        GetStructuredBuffer()->Update(&counter_value);
        cmd_list->SetStructuredBuffer(RendererBindingsUav::atomic_counter, GetStructuredBuffer());

        // Set textures
        cmd_list->SetTexture(RendererBindingsSrv::tex, tex, 0, 1);                            // top mip
        cmd_list->SetTexture(RendererBindingsUav::tex_array, tex, 1, tex->GetMipCount() - 1); // rest of the mips

        // Render
        cmd_list->Dispatch(thread_group_count_x_, thread_group_count_y_);

        cmd_list->EndMarker();
    }

    void Renderer::Pass_Ffx_Fsr2(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        cmd_list->BeginTimeblock("amd_ffx_fsr2");

        RHI_FSR2::Dispatch(
            cmd_list,
            tex_in,
            GetRenderTarget(RendererTexture::gbuffer_depth).get(),
            GetRenderTarget(RendererTexture::gbuffer_velocity).get(),
            GetRenderTarget(RendererTexture::fsr2_mask_reactive).get(),
            GetRenderTarget(RendererTexture::fsr2_mask_transparency).get(),
            tex_out,
            GetCamera().get(),
            m_cb_frame_cpu.delta_time,
            GetOption<float>(RendererOption::Sharpness)
        );

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Icons(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (!GetOption<bool>(RendererOption::Debug_Lights))
            return;

        // Acquire shaders
        RHI_Shader* shader_v = GetShader(RendererShader::quad_v).get();
        RHI_Shader* shader_p = GetShader(RendererShader::copy_bilinear_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Acquire entities
        auto& lights = m_renderables[RendererEntity::light];
        if (lights.empty() || !GetCamera())
            return;

        cmd_list->BeginTimeblock("icons");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v;
        pso.shader_pixel                    = shader_p;
        pso.rasterizer_state                = GetRasterizerState(RendererRasterizerState::cull_back_solid).get();
        pso.blend_state                     = GetBlendState(RendererBlendState::alpha).get();
        pso.depth_stencil_state             = GetDepthStencilState(RendererDepthStencilState::off_off).get();
        pso.render_target_color_textures[0] = tex_out;
        pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        bool render_pass_started = false;

        // For each light
        for (shared_ptr<Entity> entity : lights)
        {
            // Light can be null if it just got removed and our buffer doesn't update till the next frame
            if (shared_ptr<Light> light = entity->GetComponent<Light>())
            {
                const Vector3 pos_world        = entity->GetTransform()->GetPosition();
                const Vector3 pos_world_camera = GetCamera()->GetTransform()->GetPosition();
                const Vector3 camera_to_light  = (pos_world - pos_world_camera).Normalized();
                const float v_dot_l            = Vector3::Dot(GetCamera()->GetTransform()->GetForward(), camera_to_light);

                // Only draw if it's inside our view
                if (v_dot_l > 0.5f)
                {
                    if (!render_pass_started)
                    {
                        cmd_list->BeginRenderPass();
                        render_pass_started = true;
                    }

                    // Get the texture
                    RHI_Texture* texture = nullptr;
                    if (light->GetLightType() == LightType::Directional) texture = GetStandardTexture(RendererStandardTexture::gizmo_light_directional).get();
                    else if (light->GetLightType() == LightType::Point)  texture = GetStandardTexture(RendererStandardTexture::gizmo_light_point).get();
                    else if (light->GetLightType() == LightType::Spot)   texture = GetStandardTexture(RendererStandardTexture::gizmo_light_spot).get();

                    // Compute transform
                    {
                        // Use the distance from the camera to scale the icon, this will
                        // cancel out perspective scaling, hence keeping the icon scale constant.
                        const float distance = (pos_world_camera - pos_world).Length();
                        const float scale    = distance * 0.04f;

                        // 1st rotation: The quad's normal is parallel to the world's Y axis, so we rotate to make it camera facing.
                        Quaternion rotation_reorient_quad = Quaternion::FromEulerAngles(-90.0f, 0.0f, 0.0f);
                        // 2nd rotation: Rotate the camera facing quad with the camera, so that it remains a camera facing quad.
                        Quaternion rotation_camera_billboard = Quaternion::FromLookRotation(pos_world - pos_world_camera);

                        Matrix transform = Matrix(pos_world, rotation_camera_billboard * rotation_reorient_quad, scale);

                        // Update transform
                        m_cb_uber_cpu.transform = transform * m_cb_frame_cpu.view_projection;
                        UpdateConstantBufferUber(cmd_list);
                    }

                    // Draw rectangle
                    cmd_list->SetTexture(RendererBindingsSrv::tex, texture);
                    cmd_list->SetBufferVertex(GetStandardMesh(RendererStandardMesh::quad)->GetVertexBuffer());
                    cmd_list->SetBufferIndex(GetStandardMesh(RendererStandardMesh::quad)->GetIndexBuffer());
                    cmd_list->DrawIndexed(6);
                }
            }
        }

        if (render_pass_started)
        {
            cmd_list->EndRenderPass();
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_DebugMeshes(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (!GetOption<bool>(RendererOption::Debug_ReflectionProbes))
            return;

        // Acquire color shaders.
        RHI_Shader* shader_v = GetShader(RendererShader::debug_reflection_probe_v).get();
        RHI_Shader* shader_p = GetShader(RendererShader::debug_reflection_probe_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Get reflection probe entities
        const vector<shared_ptr<Entity>>& probes = m_renderables[RendererEntity::reflection_probe];
        if (probes.empty())
            return;

        cmd_list->BeginTimeblock("debug_meshes");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v;
        pso.shader_pixel                    = shader_p;
        pso.rasterizer_state                = GetRasterizerState(RendererRasterizerState::cull_back_solid).get();
        pso.blend_state                     = GetBlendState(RendererBlendState::disabled).get();
        pso.depth_stencil_state             = GetDepthStencilState(RendererDepthStencilState::r_off).get();
        pso.render_target_color_textures[0] = tex_out;
        pso.render_target_depth_texture     = GetRenderTarget(RendererTexture::gbuffer_depth).get();
        pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Render
        cmd_list->BeginRenderPass();
        {
            cmd_list->SetBufferVertex(GetStandardMesh(RendererStandardMesh::sphere)->GetVertexBuffer());
            cmd_list->SetBufferIndex(GetStandardMesh(RendererStandardMesh::sphere)->GetIndexBuffer());

            for (uint32_t probe_index = 0; probe_index < static_cast<uint32_t>(probes.size()); probe_index++)
            {
                if (shared_ptr<ReflectionProbe> probe = probes[probe_index]->GetComponent<ReflectionProbe>())
                {
                    // Set uber buffer
                    m_cb_uber_cpu.transform = probe->GetTransform()->GetMatrix();
                    UpdateConstantBufferUber(cmd_list);

                    cmd_list->SetTexture(RendererBindingsSrv::reflection_probe, probe->GetColorTexture());
                    cmd_list->DrawIndexed(GetStandardMesh(RendererStandardMesh::sphere)->GetIndexCount());

                    // Draw a box which represents the extents of the reflection probe (which is used as a geometry proxy for parallax corrected cubemap reflections)
                    BoundingBox extents = BoundingBox(probe->GetTransform()->GetPosition() - probe->GetExtents(), probe->GetTransform()->GetPosition() + probe->GetExtents());
                    DrawBox(extents);
                }
            }
        }
        cmd_list->EndRenderPass();

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Outline(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (!GetOption<bool>(RendererOption::Debug_SelectionOutline))
            return;

        // Acquire shaders
        RHI_Shader* shader_v = GetShader(RendererShader::outline_v).get();
        RHI_Shader* shader_p = GetShader(RendererShader::outline_p).get();
        RHI_Shader* shader_c = GetShader(RendererShader::outline_c).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled() || !shader_c->IsCompiled())
            return;

        if (shared_ptr<Camera> camera = Renderer::GetCamera())
        {
            if (shared_ptr<Entity> entity_selected = camera->GetSelectedEntity())
            {
                cmd_list->BeginTimeblock("outline");
                {
                    RHI_Texture* tex_outline = GetRenderTarget(RendererTexture::outline).get();
                    static const Color clear_color = Color(0.0f, 0.0f, 0.0f, 0.0f);

                    if (shared_ptr<Renderable> renderable = entity_selected->GetComponent<Renderable>())
                    {
                        if (shared_ptr<Mesh> mesh = renderable->GetMesh())
                        {
                            if (mesh->GetVertexBuffer() && mesh->GetIndexBuffer())
                            {
                                cmd_list->BeginMarker("color_silhouette");
                                {
                                    // Define render state
                                    static RHI_PipelineState pso;
                                    pso.shader_vertex                   = shader_v;
                                    pso.shader_pixel                    = shader_p;
                                    pso.rasterizer_state                = GetRasterizerState(RendererRasterizerState::cull_back_solid).get();
                                    pso.blend_state                     = GetBlendState(RendererBlendState::disabled).get();
                                    pso.depth_stencil_state             = GetDepthStencilState(RendererDepthStencilState::off_off).get();
                                    pso.render_target_color_textures[0] = tex_outline;
                                    pso.clear_color[0]                  = clear_color;
                                    pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;

                                    // Set pipeline state
                                    cmd_list->SetPipelineState(pso);

                                    // Render
                                    cmd_list->BeginRenderPass();
                                    {
                                        // Set uber buffer with entity transform
                                        m_cb_uber_cpu.transform = entity_selected->GetTransform()->GetMatrix() * m_cb_frame_cpu.view_projection_unjittered;
                                        m_cb_uber_cpu.mat_color = Math::Vector4(0.0f, 1.0f, 1.0f, 1.0f); // cyan
                                        UpdateConstantBufferUber(cmd_list);

                                        cmd_list->SetBufferVertex(mesh->GetVertexBuffer());
                                        cmd_list->SetBufferIndex(mesh->GetIndexBuffer());
                                        cmd_list->DrawIndexed(renderable->GetIndexCount(), renderable->GetIndexOffset(), renderable->GetVertexOffset());
                                        cmd_list->EndRenderPass();
                                    }
                                }
                                cmd_list->EndMarker();

                                // Blur the color silhouette
                                {
                                    const bool depth_aware = false;
                                    const float radius     = 30.0f;
                                    const float sigma      = 32.0f;
                                    Pass_Blur_Gaussian(cmd_list, tex_outline, depth_aware, radius, sigma);
                                }

                                // Combine color silhouette with frame
                                cmd_list->BeginMarker("composition");
                                {
                                    static RHI_PipelineState pso;
                                    pso.shader_compute = shader_c;

                                    // Set pipeline state
                                    cmd_list->SetPipelineState(pso);

                                    // Set uber buffer
                                    m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
                                    UpdateConstantBufferUber(cmd_list);

                                    // Set textures
                                    cmd_list->SetTexture(RendererBindingsUav::tex, tex_out);
                                    cmd_list->SetTexture(RendererBindingsSrv::tex, tex_outline);

                                    // Render
                                    cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));
                                }
                                cmd_list->EndMarker();
                            }
                        }
                    }
                }
                cmd_list->EndTimeblock();
            }
        }
    }

    void Renderer::Pass_PeformanceMetrics(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        // Early exit cases
        const bool draw      = GetOption<bool>(RendererOption::Debug_PerformanceMetrics);
        const bool empty     = Profiler::GetMetrics().empty();
        const auto& shader_v = GetShader(RendererShader::font_v);
        const auto& shader_p = GetShader(RendererShader::font_p);
        if (!draw || empty || !shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // If the performance metrics are being drawn, the profiler has to be enabled.
        if (!Profiler::GetEnabled())
        {
            Profiler::SetEnabled(true);
        }

        // Update text
        const Vector2 text_pos = Vector2(-GetViewport().width * 0.5f + 5.0f, GetViewport().height * 0.5f - m_font->GetSize() - 2.0f);
        m_font->SetText(Profiler::GetMetrics(), text_pos);

        cmd_list->BeginMarker("performance_metrics");
        cmd_list->BeginTimeblock("outline");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v.get();
        pso.shader_pixel                    = shader_p.get();
        pso.rasterizer_state                = GetRasterizerState(RendererRasterizerState::cull_back_solid).get();
        pso.blend_state                     = GetBlendState(RendererBlendState::alpha).get();
        pso.depth_stencil_state             = GetDepthStencilState(RendererDepthStencilState::off_off).get();
        pso.render_target_color_textures[0] = tex_out;
        pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;

        // Draw outline
        if (m_font->GetOutline() != Font_Outline_None && m_font->GetOutlineSize() != 0)
        {
            cmd_list->SetPipelineState(pso);
            cmd_list->BeginRenderPass();
            {
                // Set uber buffer
                m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
                m_cb_uber_cpu.mat_color     = m_font->GetColorOutline();
                UpdateConstantBufferUber(cmd_list);

                cmd_list->SetBufferIndex(m_font->GetIndexBuffer());
                cmd_list->SetBufferVertex(m_font->GetVertexBuffer());
                cmd_list->SetTexture(RendererBindingsSrv::font_atlas, m_font->GetAtlasOutline());
                cmd_list->DrawIndexed(m_font->GetIndexCount());
                cmd_list->EndRenderPass();
            }
        }

        cmd_list->EndTimeblock();
        cmd_list->BeginTimeblock("inline");

        // Draw
        cmd_list->SetPipelineState(pso);
        cmd_list->BeginRenderPass();
        {
            // Set uber buffer
            m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            m_cb_uber_cpu.mat_color     = m_font->GetColor();
            UpdateConstantBufferUber(cmd_list);

            cmd_list->SetBufferIndex(m_font->GetIndexBuffer());
            cmd_list->SetBufferVertex(m_font->GetVertexBuffer());
            cmd_list->SetTexture(RendererBindingsSrv::font_atlas, m_font->GetAtlas());
            cmd_list->DrawIndexed(m_font->GetIndexCount());
            cmd_list->EndRenderPass();
        }

        cmd_list->EndTimeblock();
        cmd_list->EndMarker();
    }

    void Renderer::Pass_BrdfSpecularLut(RHI_CommandList* cmd_list)
    {
        // Acquire shader
        RHI_Shader* shader_c = GetShader(RendererShader::brdf_specular_lut_c).get();
        if (!shader_c->IsCompiled())
            return;

        // Acquire render target
        RHI_Texture* tex_brdf_specular_lut = GetRenderTarget(RendererTexture::brdf_specular_lut).get();

        // Define render state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        cmd_list->BeginTimeblock("brdf_specular_lut");

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Set uber buffer
        m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_brdf_specular_lut->GetWidth()), static_cast<float>(tex_brdf_specular_lut->GetHeight()));
        UpdateConstantBufferUber(cmd_list);

        // Set texture
        cmd_list->SetTexture(RendererBindingsUav::tex, tex_brdf_specular_lut);

        // Render
        cmd_list->Dispatch(thread_group_count_x(tex_brdf_specular_lut), thread_group_count_y(tex_brdf_specular_lut));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Copy(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out, const bool bilinear)
    {
        // Acquire shaders
        RHI_Shader* shader_c = GetShader(bilinear ? RendererShader::copy_bilinear_c : RendererShader::copy_point_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginMarker(bilinear ? "copy_bilinear" : "copy_point");

        // Define render state
        static RHI_PipelineState pso;
        pso.shader_compute  = shader_c;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Set uber buffer
        m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateConstantBufferUber(cmd_list);

        cmd_list->SetTexture(RendererBindingsUav::tex, tex_out);
        cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);

        // Render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndMarker();
    }

    void Renderer::Pass_CopyToBackbuffer()
    {
        // Acquire shaders
        RHI_Shader* shader_v = GetShader(RendererShader::fullscreen_triangle_v).get();
        RHI_Shader* shader_p = GetShader(RendererShader::copy_point_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        m_cmd_current->BeginMarker("copy_to_back_buffer");

        // Transition swap chain image
        m_swap_chain->SetLayout(RHI_Image_Layout::Color_Attachment_Optimal, m_cmd_current);

        // Define render state
        static RHI_PipelineState pso = {};
        pso.shader_vertex            = shader_v;
        pso.shader_pixel             = shader_p;
        pso.rasterizer_state         = GetRasterizerState(RendererRasterizerState::cull_back_solid).get();
        pso.blend_state              = GetBlendState(RendererBlendState::disabled).get();
        pso.depth_stencil_state      = GetDepthStencilState(RendererDepthStencilState::off_off).get();
        pso.render_target_swapchain  = m_swap_chain.get();
        pso.clear_color[0]           = rhi_color_dont_care;
        pso.primitive_topology       = RHI_PrimitiveTopology_Mode::TriangleList;

        // Set pipeline state
        m_cmd_current->SetPipelineState(pso);

        // Set uber buffer
        m_cb_uber_cpu.resolution_rt = Vector2(static_cast<float>(m_swap_chain->GetWidth()), static_cast<float>(m_swap_chain->GetHeight()));
        UpdateConstantBufferUber(m_cmd_current);

        // Set texture
        m_cmd_current->SetTexture(RendererBindingsSrv::tex, GetRenderTarget(RendererTexture::frame_output).get());

        // Render
        m_cmd_current->BeginRenderPass();
        {
            m_cmd_current->DrawIndexed(3, 0);
        }
        m_cmd_current->EndRenderPass();

        // Transition swap chain image
        m_swap_chain->SetLayout(RHI_Image_Layout::Present_Src, m_cmd_current);

        m_cmd_current->EndMarker();
    }
}
