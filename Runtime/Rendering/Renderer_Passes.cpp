/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ==============================
#include "Spartan.h"
#include "Renderer.h"
#include "Model.h"
#include "ShaderGBuffer.h"
#include "ShaderLight.h"
#include "Font/Font.h"
#include "Gizmos/Grid.h"
#include "Gizmos/TransformGizmo.h"
#include "../Profiling/Profiler.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Implementation.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_PipelineState.h"
#include "../RHI/RHI_Texture.h"
#include "../RHI/RHI_SwapChain.h"
#include "../World/Entity.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Renderable.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    void Renderer::SetGlobalSamplersAndConstantBuffers(RHI_CommandList* cmd_list) const
    {
        // Constant buffers
        cmd_list->SetConstantBuffer(0, RHI_Shader_Vertex | RHI_Shader_Pixel | RHI_Shader_Compute, m_buffer_frame_gpu);
        cmd_list->SetConstantBuffer(1, RHI_Shader_Compute, m_buffer_material_gpu);
        cmd_list->SetConstantBuffer(2, RHI_Shader_Vertex | RHI_Shader_Pixel | RHI_Shader_Compute, m_buffer_uber_gpu);
        cmd_list->SetConstantBuffer(3, RHI_Shader_Compute, m_buffer_light_gpu);
        
        // Samplers
        cmd_list->SetSampler(0, m_sampler_compare_depth);
        cmd_list->SetSampler(1, m_sampler_point_clamp);
        cmd_list->SetSampler(2, m_sampler_point_wrap);
        cmd_list->SetSampler(3, m_sampler_bilinear_clamp);
        cmd_list->SetSampler(4, m_sampler_bilinear_wrap);
        cmd_list->SetSampler(5, m_sampler_trilinear_clamp);
        cmd_list->SetSampler(6, m_sampler_anisotropic_wrap);
    }

    void Renderer::Pass_Main(RHI_CommandList* cmd_list)
    {
        // Validate cmd list
        SP_ASSERT(cmd_list != nullptr);
        SP_ASSERT(cmd_list->GetState() == RHI_CommandListState::Recording);

        SCOPED_TIME_BLOCK(m_profiler);

        // Update frame constant buffer
        Pass_UpdateFrameBuffer(cmd_list);

        // Generate brdf specular lut (only runs once)
        Pass_BrdfSpecularLut(cmd_list);

        const bool draw_transparent_objects = !m_entities[Renderer_Object_Transparent].empty();

        // Depth
        {
            Pass_Depth_Light(cmd_list, Renderer_Object_Opaque);
            if (draw_transparent_objects)
            {
                Pass_Depth_Light(cmd_list, Renderer_Object_Transparent);
            }
        
            if (GetOption(Render_DepthPrepass))
            {
                Pass_Depth_Prepass(cmd_list);
            }
        }

        // Acquire render targets
        RHI_Texture* rt1 = RENDER_TARGET(RendererRt::Frame).get();
        RHI_Texture* rt2 = RENDER_TARGET(RendererRt::Frame_2).get();

        // G-Buffer and lighting
        {
            // G-buffer
            Pass_GBuffer(cmd_list);

            // Passes which really on the G-buffer
            Pass_Ssao(cmd_list);
            Pass_Reflections_SsrTrace(cmd_list);
            Pass_Ssgi(cmd_list);

            // Lighting
            Pass_Light(cmd_list);

            // Injection of SSGI into the light buffers
            Pass_Ssgi_Inject(cmd_list);

            // Composition of the light buffers (including volumetric fog)
            Pass_Light_Composition(cmd_list, rt1);

            // Image based lighting
            Pass_Light_ImageBased(cmd_list, rt1);

            // If SSR is enabled, copy the frame so that SSR can use it to reflect from
            if ((m_options & Render_ScreenSpaceReflections) != 0)
            {
                Pass_Copy(cmd_list, rt1, rt2);
            }

            // Reflections - SSR & Environment
            Pass_Reflections(cmd_list, rt1, rt2);

            // Lighting for transparent objects (a simpler version of the above)
            if (draw_transparent_objects)
            {
                // Copy the frame so that refraction can sample from it
                Pass_Copy(cmd_list, rt1, rt2);

                const bool is_trasparent_pass = true;
                Pass_GBuffer(cmd_list, is_trasparent_pass);
                Pass_Light(cmd_list, is_trasparent_pass);
                Pass_Light_Composition(cmd_list, rt1, is_trasparent_pass);
                Pass_Light_ImageBased(cmd_list, rt1, is_trasparent_pass);
            }
        }

        Pass_PostProcess(cmd_list);
    }

    void Renderer::Pass_UpdateFrameBuffer(RHI_CommandList* cmd_list)
    {
        // Set render state
        static RHI_PipelineState pso;
        pso.pass_name = "Pass_UpdateFrameBuffer";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            UpdateFrameBuffer(cmd_list);
            cmd_list->EndRenderPass();
        }

    }

    void Renderer::Pass_Depth_Light(RHI_CommandList* cmd_list, const Renderer_Object_Type object_type)
    {
        // All opaque objects are rendered from the lights point of view.
        // Opaque objects write their depth information to a depth buffer, using just a vertex shader.
        // Transparent objects, read the opaque depth but don't write their own, instead, they write their color information using a pixel shader.

        // Acquire shader
        RHI_Shader* shader_v = m_shaders[RendererShader::Depth_V].get();
        RHI_Shader* shader_p = m_shaders[RendererShader::Depth_P].get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Get entities
        const auto& entities = m_entities[object_type];
        if (entities.empty())
            return;

        const bool transparent_pass = object_type == Renderer_Object_Transparent;

        // Go through all of the lights
        const auto& entities_light = m_entities[Renderer_Object_Light];
        for (uint32_t light_index = 0; light_index < entities_light.size(); light_index++)
        {
            const Light* light = entities_light[light_index]->GetComponent<Light>();

            // Can happen when loading a new scene and the lights get deleted
            if (!light)
                continue;

            // Skip lights which don't cast shadows or have an intensity of zero
            if (!light->GetShadowsEnabled() || light->GetIntensity() == 0.0f)
                continue;

            // Skip lights that don't cast transparent shadows (if this is a transparent pass)
            if (transparent_pass && !light->GetShadowsTransparentEnabled())
                continue;

            // Acquire light's shadow maps
            RHI_Texture* tex_depth = light->GetDepthTexture();
            RHI_Texture* tex_color = light->GetColorTexture();
            if (!tex_depth)
                continue;

            // Set render state
            static RHI_PipelineState pso;
            pso.shader_vertex                    = shader_v;
            pso.vertex_buffer_stride             = static_cast<uint32_t>(sizeof(RHI_Vertex_PosTexNorTan)); // assume all vertex buffers have the same stride (which they do)
            pso.shader_pixel                     = transparent_pass ? shader_p : nullptr;
            pso.blend_state                      = transparent_pass ? m_blend_alpha.get() : m_blend_disabled.get();
            pso.depth_stencil_state              = transparent_pass ? m_depth_stencil_r_off.get() : m_depth_stencil_rw_off.get();
            pso.render_target_color_textures[0]  = tex_color; // always bind so we can clear to white (in case there are now transparent objects)
            pso.render_target_depth_texture      = tex_depth;
            pso.clear_stencil                    = rhi_stencil_dont_care;
            pso.viewport                         = tex_depth->GetViewport();
            pso.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
            pso.pass_name                        = transparent_pass ? "Pass_Depth_Light_Transparent" : "Pass_Depth_Light";

            for (uint32_t array_index = 0; array_index < tex_depth->GetArraySize(); array_index++)
            {
                // Set render target texture array index
                pso.render_target_color_texture_array_index          = array_index;
                pso.render_target_depth_stencil_texture_array_index  = array_index;

                // Set clear values
                pso.clear_color[0] = Vector4::One;
                pso.clear_depth    = transparent_pass ? rhi_depth_load : GetClearDepth();

                const Matrix& view_projection = light->GetViewMatrix(array_index) * light->GetProjectionMatrix(array_index);

                // Set appropriate rasterizer state
                if (light->GetLightType() == LightType::Directional)
                {
                    // "Pancaking" - https://www.gamedev.net/forums/topic/639036-shadow-mapping-and-high-up-objects/
                    // It's basically a way to capture the silhouettes of potential shadow casters behind the light's view point.
                    // Of course we also have to make sure that the light doesn't cull them in the first place (this is done automatically by the light)
                    pso.rasterizer_state = m_rasterizer_light_directional.get();
                }
                else
                {
                    pso.rasterizer_state = m_rasterizer_light_point_spot.get();
                }

                // State tracking
                bool render_pass_active     = false;
                uint32_t m_set_material_id  = 0;

                for (uint32_t entity_index = 0; entity_index < static_cast<uint32_t>(entities.size()); entity_index++)
                {
                    Entity* entity = entities[entity_index];

                    // Acquire renderable component
                    Renderable* renderable = entity->GetRenderable();
                    if (!renderable)
                        continue;

                    // Skip meshes that don't cast shadows
                    if (!renderable->GetCastShadows())
                        continue;

                    // Acquire geometry
                    Model* model = renderable->GeometryModel();
                    if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
                        continue;

                    // Acquire material
                    Material* material = renderable->GetMaterial();
                    if (!material)
                        continue;

                    // Skip objects outside of the view frustum
                    if (!light->IsInViewFrustrum(renderable, array_index))
                        continue;

                    if (!render_pass_active)
                    {
                        render_pass_active = cmd_list->BeginRenderPass(pso);
                    }

                    // Bind material
                    if (transparent_pass && m_set_material_id != material->GetId())
                    {
                        // Bind material textures
                        RHI_Texture* tex_albedo = material->GetTexture_Ptr(Material_Color);
                        cmd_list->SetTexture(RendererBindingsSrv::tex, tex_albedo ? tex_albedo : m_tex_default_white.get());

                        // Update uber buffer with material properties
                        m_buffer_uber_cpu.mat_albedo    = material->GetColorAlbedo();
                        m_buffer_uber_cpu.mat_tiling_uv = material->GetTiling();
                        m_buffer_uber_cpu.mat_offset_uv = material->GetOffset();

                        m_set_material_id = material->GetId();
                    }

                    // Bind geometry
                    cmd_list->SetBufferIndex(model->GetIndexBuffer());
                    cmd_list->SetBufferVertex(model->GetVertexBuffer());

                    // Update uber buffer with cascade transform
                    m_buffer_uber_cpu.transform = entity->GetTransform()->GetMatrix() * view_projection;
                    if (!UpdateUberBuffer(cmd_list))
                        continue;

                    cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
                }

                if (render_pass_active)
                {
                    cmd_list->EndRenderPass();
                }
            }
        }
    }

    void Renderer::Pass_Depth_Prepass(RHI_CommandList* cmd_list)
    {
        // Description: All the opaque meshes are rendered, outputting
        // just their depth information into a depth map.

        // Acquire required resources/data
        const auto& shader_depth    = m_shaders[RendererShader::Depth_V];
        const auto& tex_depth       = RENDER_TARGET(RendererRt::Gbuffer_Depth);
        const auto& entities        = m_entities[Renderer_Object_Opaque];

        // Ensure the shader has compiled
        if (!shader_depth->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_vertex                = shader_depth.get();
        pso.shader_pixel                 = nullptr;
        pso.rasterizer_state             = m_rasterizer_cull_back_solid.get();
        pso.blend_state                  = m_blend_disabled.get();
        pso.depth_stencil_state          = m_depth_stencil_rw_off.get();
        pso.render_target_depth_texture  = tex_depth.get();
        pso.clear_depth                  = GetClearDepth();
        pso.viewport                     = tex_depth->GetViewport();
        pso.primitive_topology           = RHI_PrimitiveTopology_TriangleList;
        pso.pass_name                    = "Pass_Depth_Prepass";

        // Record commands
        if (cmd_list->BeginRenderPass(pso))
        { 
            if (!entities.empty())
            {
                // Variables that help reduce state changes
                uint32_t currently_bound_geometry = 0;

                // Draw opaque
                for (const auto& entity : entities)
                {
                    // Get renderable
                    Renderable* renderable = entity->GetRenderable();
                    if (!renderable)
                        continue;

                    // Get geometry
                    Model* model = renderable->GeometryModel();
                    if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
                        continue;

                    // Skip objects outside of the view frustum
                    if (!m_camera->IsInViewFrustrum(renderable))
                        continue;

                    // Bind geometry
                    if (currently_bound_geometry != model->GetId())
                    {
                        cmd_list->SetBufferIndex(model->GetIndexBuffer());
                        cmd_list->SetBufferVertex(model->GetVertexBuffer());
                        currently_bound_geometry = model->GetId();
                    }

                    // Update uber buffer with entity transform
                    if (Transform* transform = entity->GetTransform())
                    {
                        // Update uber buffer with cascade transform
                        m_buffer_uber_cpu.transform = transform->GetMatrix() * m_buffer_frame_cpu.view_projection;
                        UpdateUberBuffer(cmd_list);
                    }

                    // Draw    
                    cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
                }
            }
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_GBuffer(RHI_CommandList* cmd_list, const bool is_transparent_pass /*= false*/)
    {
        // Acquire required resources/shaders
        RHI_Texture* tex_albedo       = RENDER_TARGET(RendererRt::Gbuffer_Albedo).get();
        RHI_Texture* tex_normal       = RENDER_TARGET(RendererRt::Gbuffer_Normal).get();
        RHI_Texture* tex_material     = RENDER_TARGET(RendererRt::Gbuffer_Material).get();
        RHI_Texture* tex_velocity     = RENDER_TARGET(RendererRt::Gbuffer_Velocity).get();
        RHI_Texture* tex_depth        = RENDER_TARGET(RendererRt::Gbuffer_Depth).get();
        RHI_Shader* shader_v          = m_shaders[RendererShader::Gbuffer_V].get();
        ShaderGBuffer* shader_p       = static_cast<ShaderGBuffer*>(m_shaders[RendererShader::Gbuffer_P].get());

        // Validate that the shader has compiled
        if (!shader_v->IsCompiled())
            return;

        // Set render state
        RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v;
        pso.blend_state                     = m_blend_disabled.get();
        pso.rasterizer_state                = GetOption(Render_Debug_Wireframe) ? m_rasterizer_cull_back_wireframe.get() : m_rasterizer_cull_back_solid.get();
        pso.depth_stencil_state             = is_transparent_pass ? m_depth_stencil_rw_w.get() : (GetOption(Render_DepthPrepass) ? m_depth_stencil_r_off.get() : m_depth_stencil_rw_off.get());
        pso.render_target_color_textures[0] = tex_albedo;
        pso.clear_color[0]                  = !is_transparent_pass ? Vector4::Zero : rhi_color_load;
        pso.render_target_color_textures[1] = tex_normal;
        pso.clear_color[1]                  = !is_transparent_pass ? Vector4::Zero : rhi_color_load;
        pso.render_target_color_textures[2] = tex_material;
        pso.clear_color[2]                  = !is_transparent_pass ? Vector4::Zero : rhi_color_load;
        pso.render_target_color_textures[3] = tex_velocity;
        pso.clear_color[3]                  = !is_transparent_pass ? Vector4::Zero : rhi_color_load;
        pso.render_target_depth_texture     = tex_depth;
        pso.clear_depth                     = (is_transparent_pass || GetOption(Render_DepthPrepass)) ? rhi_depth_load : GetClearDepth();
        pso.clear_stencil                   = !is_transparent_pass ? 0 : rhi_stencil_dont_care;
        pso.viewport                        = tex_albedo->GetViewport();
        pso.vertex_buffer_stride            = static_cast<uint32_t>(sizeof(RHI_Vertex_PosTexNorTan)); // assume all vertex buffers have the same stride (which they do)
        pso.primitive_topology              = RHI_PrimitiveTopology_TriangleList;

        bool cleared = false;
        uint32_t material_index = 0;
        uint32_t material_bound_id = 0;
        m_material_instances.fill(nullptr);

        // Iterate through all the G-Buffer shader variations
        for (const auto& it : ShaderGBuffer::GetVariations())
        {
            // Set pixel shader
            pso.shader_pixel = static_cast<RHI_Shader*>(it.second.get());

            // Skip the shader it failed to compiled or hasn't compiled yet
            if (!pso.shader_pixel->IsCompiled())
                continue;

            // Set pass name
            pso.pass_name = is_transparent_pass ? "GBuffer_Transparent" : "GBuffer_Opaque";

            bool render_pass_active = false;
            auto& entities = m_entities[is_transparent_pass ? Renderer_Object_Transparent : Renderer_Object_Opaque];

            // Record commands
            for (uint32_t i = 0; i < static_cast<uint32_t>(entities.size()); i++)
            {
                Entity* entity = entities[i];

                // Get renderable
                Renderable* renderable = entity->GetRenderable();
                if (!renderable)
                    continue;

                // Get material
                Material* material = renderable->GetMaterial();
                if (!material)
                    continue;

                // Skip objects with different shader requirements
                if (!static_cast<ShaderGBuffer*>(pso.shader_pixel)->IsSuitable(material->GetFlags()))
                    continue;

                // Skip transparent objects that won't contribute
                if (material->GetColorAlbedo().w == 0 && is_transparent_pass)
                    continue;

                // Get geometry
                Model* model = renderable->GeometryModel();
                if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
                    continue;

                // Skip objects outside of the view frustum
                if (!m_camera->IsInViewFrustrum(renderable))
                    continue;

                if (!render_pass_active)
                {
                    // Reset clear values after the first render pass
                    if (cleared)
                    {
                        pso.ResetClearValues();
                    }

                    render_pass_active = cmd_list->BeginRenderPass(pso);

                    cleared = true;
                }

                // Set geometry (will only happen if not already set)
                cmd_list->SetBufferIndex(model->GetIndexBuffer());
                cmd_list->SetBufferVertex(model->GetVertexBuffer());

                // Bind material
                const bool firs_run       = material_index == 0;
                const bool new_material   = material_bound_id != material->GetId();
                if (firs_run || new_material)
                {
                    material_bound_id = material->GetId();

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
                        LOG_ERROR("Material instance array has reached it's maximum capacity of %d elements. Consider increasing the size.", m_max_material_instances);
                    }

                    // Bind material textures
                    cmd_list->SetTexture(RendererBindingsSrv::material_albedo,      material->GetTexture_Ptr(Material_Color));
                    cmd_list->SetTexture(RendererBindingsSrv::material_roughness,   material->GetTexture_Ptr(Material_Roughness));
                    cmd_list->SetTexture(RendererBindingsSrv::material_metallic,    material->GetTexture_Ptr(Material_Metallic));
                    cmd_list->SetTexture(RendererBindingsSrv::material_normal,      material->GetTexture_Ptr(Material_Normal));
                    cmd_list->SetTexture(RendererBindingsSrv::material_height,      material->GetTexture_Ptr(Material_Height));
                    cmd_list->SetTexture(RendererBindingsSrv::material_occlusion,   material->GetTexture_Ptr(Material_Occlusion));
                    cmd_list->SetTexture(RendererBindingsSrv::material_emission,    material->GetTexture_Ptr(Material_Emission));
                    cmd_list->SetTexture(RendererBindingsSrv::material_mask,        material->GetTexture_Ptr(Material_Mask));
                
                    // Update uber buffer with material properties
                    m_buffer_uber_cpu.mat_id            = static_cast<float>(material_index);
                    m_buffer_uber_cpu.mat_albedo        = material->GetColorAlbedo();
                    m_buffer_uber_cpu.mat_tiling_uv     = material->GetTiling();
                    m_buffer_uber_cpu.mat_offset_uv     = material->GetOffset();
                    m_buffer_uber_cpu.mat_roughness_mul = material->GetProperty(Material_Roughness);
                    m_buffer_uber_cpu.mat_metallic_mul  = material->GetProperty(Material_Metallic);
                    m_buffer_uber_cpu.mat_normal_mul    = material->GetProperty(Material_Normal);
                    m_buffer_uber_cpu.mat_height_mul    = material->GetProperty(Material_Height);

                    // Update constant buffer
                    UpdateUberBuffer(cmd_list);
                }
                
                // Update uber buffer with entity transform
                if (Transform* transform = entity->GetTransform())
                {
                    m_buffer_uber_cpu.transform             = transform->GetMatrix();
                    m_buffer_uber_cpu.transform_previous    = transform->GetMatrixPrevious();

                    // Save matrix for velocity computation
                    transform->SetWvpLastFrame(m_buffer_uber_cpu.transform);

                    // Update object buffer
                    if (!UpdateUberBuffer(cmd_list))
                        continue;
                }
                
                // Render
                cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
                m_profiler->m_renderer_meshes_rendered++;
            }

            if (render_pass_active)
            {
                cmd_list->EndRenderPass();
            }
        }
    }

    void Renderer::Pass_Ssgi(RHI_CommandList* cmd_list)
    {
        if ((m_options & Render_Ssgi) == 0)
            return;

        // Acquire shaders
        RHI_Shader* shader_ssgi             = m_shaders[RendererShader::Ssgi_C].get();
        RHI_Shader* shader_ssgi_accumulate  = m_shaders[RendererShader::Ssgi_Accumulate_C].get();
        if (!shader_ssgi->IsCompiled() || !shader_ssgi_accumulate->IsCompiled())
            return;

        // Get render targets
        shared_ptr<RHI_Texture> tex_ssgi_raw               = RENDER_TARGET(RendererRt::Ssgi_Raw);
        shared_ptr<RHI_Texture> tex_ssgi_history           = RENDER_TARGET(RendererRt::Ssgi_History_1);
        shared_ptr<RHI_Texture> tex_ssgi_history_blurred   = RENDER_TARGET(RendererRt::Ssgi_History_2);

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute   = shader_ssgi;
        pso.pass_name        = "Pass_Ssgi";

        // SSGI
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(tex_ssgi_raw->GetWidth(), tex_ssgi_raw->GetHeight());
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_ssgi_raw->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_ssgi_raw->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z   = 1;
            const bool async                      = false;

            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_albedo,   RENDER_TARGET(RendererRt::Gbuffer_Albedo));
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal,   RENDER_TARGET(RendererRt::Gbuffer_Normal));
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_velocity, RENDER_TARGET(RendererRt::Gbuffer_Velocity));
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth,    RENDER_TARGET(RendererRt::Gbuffer_Depth));
            cmd_list->SetTexture(RendererBindingsSrv::light_diffuse,    RENDER_TARGET(RendererRt::Light_Diffuse));
            cmd_list->SetTexture(RendererBindingsUav::rgb,              tex_ssgi_raw);

            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }

        // Accumulate
        pso.shader_compute  = shader_ssgi_accumulate;
        pso.pass_name       = "Pass_Ssgi_Accumulate";
        if (cmd_list->BeginRenderPass(pso))
        {
            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_ssgi_history->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_ssgi_history->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsSrv::tex,  tex_ssgi_raw);
            cmd_list->SetTexture(RendererBindingsSrv::tex2, tex_ssgi_history_blurred);
            cmd_list->SetTexture(RendererBindingsUav::rgb,  tex_ssgi_history);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }

        // Update history buffer with blurred version
        const auto sigma = 2.0f;
        const auto pixel_stride = 2.0f;
        Pass_Blur_BilateralGaussian(cmd_list, tex_ssgi_history, tex_ssgi_history_blurred, sigma, pixel_stride, false);
    }

    void Renderer::Pass_Ssgi_Inject(RHI_CommandList* cmd_list)
    {
        if ((m_options & Render_Ssgi) == 0)
            return;

        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::SsgiInject_C].get();
        if (!shader_c->IsCompiled())
            return;

        RHI_Texture* tex_out = RENDER_TARGET(RendererRt::Light_Diffuse).get();

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute   = shader_c;
        pso.pass_name        = "Pass_Ssgi_Inject";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(tex_out->GetWidth(), tex_out->GetHeight());
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z   = 1;
            const bool async                      = false;

            cmd_list->SetTexture(RendererBindingsUav::rgb, tex_out); // diffuse
            cmd_list->SetTexture(RendererBindingsSrv::ssgi, RENDER_TARGET(RendererRt::Ssgi_History_1));

            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_Ssao(RHI_CommandList* cmd_list)
    {
        if ((m_options & Render_Ssao) == 0)
            return;

        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::Ssao_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Acquire textures
        shared_ptr<RHI_Texture>& tex_ssao_noisy     = RENDER_TARGET(RendererRt::Ssao);
        shared_ptr<RHI_Texture>& tex_ssao_blurred   = RENDER_TARGET(RendererRt::Ssao_Blurred);
        RHI_Texture* tex_depth                      = RENDER_TARGET(RendererRt::Gbuffer_Depth).get();
        RHI_Texture* tex_normal                     = RENDER_TARGET(RendererRt::Gbuffer_Normal).get();

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute   = shader_c;
        pso.pass_name        = "Pass_Ssao";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_ssao_noisy->GetWidth()), static_cast<float>(tex_ssao_noisy->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_ssao_noisy->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_ssao_noisy->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal, tex_normal);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
            cmd_list->SetTexture(RendererBindingsUav::r, tex_ssao_noisy);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }

        // Bilateral blur
        const auto sigma = 2.0f;
        const auto pixel_stride = 2.0f;
        Pass_Blur_BilateralGaussian(cmd_list, tex_ssao_noisy, tex_ssao_blurred, sigma, pixel_stride, false);
    }

    void Renderer::Pass_Reflections_SsrTrace(RHI_CommandList* cmd_list)
    {
        if ((m_options & Render_ScreenSpaceReflections) == 0)
            return;

        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::SsrTrace_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Acquire render targets
        RHI_Texture* tex_out = RENDER_TARGET(RendererRt::Ssr).get();

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute   = shader_c;
        pso.pass_name        = "Pass_Reflections_SsrTrace";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba,             tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal,   RENDER_TARGET(RendererRt::Gbuffer_Normal));
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth,    RENDER_TARGET(RendererRt::Gbuffer_Depth));
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_material, RENDER_TARGET(RendererRt::Gbuffer_Material));
            cmd_list->SetTexture(RendererBindingsSrv::noise_normal,     m_tex_default_noise_normal);
            cmd_list->SetTexture(RendererBindingsSrv::noise_blue,       m_tex_default_noise_blue);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_Reflections(RHI_CommandList* cmd_list, RHI_Texture* tex_out, RHI_Texture* tex_reflections)
    {
        // Acquire shaders
        RHI_Shader* shader_v = m_shaders[RendererShader::Quad_V].get();
        RHI_Shader* shader_p = m_shaders[RendererShader::Reflections_P].get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v;
        pso.shader_pixel                    = shader_p;
        pso.rasterizer_state                = m_rasterizer_cull_back_solid.get();
        pso.depth_stencil_state             = m_depth_stencil_off_off.get();
        pso.blend_state                     = m_blend_additive.get();
        pso.render_target_color_textures[0] = tex_out;
        pso.clear_color[0]                  = rhi_color_load;
        pso.render_target_depth_texture     = nullptr;
        pso.clear_depth                     = rhi_depth_dont_care;
        pso.clear_stencil                   = rhi_stencil_dont_care;
        pso.viewport                        = tex_out->GetViewport();
        pso.vertex_buffer_stride            = m_viewport_quad.GetVertexBuffer()->GetStride();
        pso.primitive_topology              = RHI_PrimitiveTopology_TriangleList;
        pso.pass_name                       = "Pass_Reflections";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_albedo,   RENDER_TARGET(RendererRt::Gbuffer_Albedo));
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal,   RENDER_TARGET(RendererRt::Gbuffer_Normal));
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_material, RENDER_TARGET(RendererRt::Gbuffer_Material));
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth,    RENDER_TARGET(RendererRt::Gbuffer_Depth));
            cmd_list->SetTexture(RendererBindingsSrv::ssr,              RENDER_TARGET(RendererRt::Ssr));
            cmd_list->SetTexture(RendererBindingsSrv::frame,            tex_reflections);
            cmd_list->SetTexture(RendererBindingsSrv::environment,      GetEnvironmentTexture());

            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_Light(RHI_CommandList* cmd_list, const bool is_transparent_pass /*= false*/)
    {
        // Acquire lights
        const vector<Entity*>& entities = m_entities[Renderer_Object_Light];
        if (entities.empty())
            return;

        // Acquire render targets
        RHI_Texture* tex_diffuse    = is_transparent_pass ? RENDER_TARGET(RendererRt::Light_Diffuse_Transparent).get()   : RENDER_TARGET(RendererRt::Light_Diffuse).get();
        RHI_Texture* tex_specular   = is_transparent_pass ? RENDER_TARGET(RendererRt::Light_Specular_Transparent).get()  : RENDER_TARGET(RendererRt::Light_Specular).get();
        RHI_Texture* tex_volumetric = RENDER_TARGET(RendererRt::Light_Volumetric).get();

        // Clear render targets
        cmd_list->ClearRenderTarget(tex_diffuse,    0, 0, true, Vector4::Zero);
        cmd_list->ClearRenderTarget(tex_specular,   0, 0, true, Vector4::Zero);
        cmd_list->ClearRenderTarget(tex_volumetric, 0, 0, true, Vector4::Zero);

        // Set render state
        static RHI_PipelineState pso;
        pso.pass_name = is_transparent_pass ? "Pass_Light_Transparent" : "Pass_Light_Opaque";

        // Iterate through all the light entities
        for (const auto& entity : entities)
        {
            if (Light* light = entity->GetComponent<Light>())
            {
                if (light->GetIntensity() != 0)
                {
                    // Set pixel shader
                    pso.shader_compute = static_cast<RHI_Shader*>(ShaderLight::GetVariation(m_context, light, m_options));

                    // Skip the shader it failed to compiled or hasn't compiled yet
                    if (!pso.shader_compute->IsCompiled())
                        continue;

                    // Draw
                    if (cmd_list->BeginRenderPass(pso))
                    {
                        // Update constant buffer (light pass will access it using material IDs)
                        UpdateMaterialBuffer(cmd_list);

                        cmd_list->SetTexture(RendererBindingsUav::rgb,              tex_diffuse);
                        cmd_list->SetTexture(RendererBindingsUav::rgb2,             tex_specular);
                        cmd_list->SetTexture(RendererBindingsUav::rgb3,             tex_volumetric);
                        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_albedo,   RENDER_TARGET(RendererRt::Gbuffer_Albedo));
                        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal,   RENDER_TARGET(RendererRt::Gbuffer_Normal));
                        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_material, RENDER_TARGET(RendererRt::Gbuffer_Material));
                        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth,    RENDER_TARGET(RendererRt::Gbuffer_Depth));
                        cmd_list->SetTexture(RendererBindingsSrv::ssao,             (m_options & Render_Ssao) ? RENDER_TARGET(RendererRt::Ssao_Blurred) : m_tex_default_white);
                        cmd_list->SetTexture(RendererBindingsSrv::noise_blue,       m_tex_default_noise_blue);
                        
                        // Set shadow map
                        if (light->GetShadowsEnabled())
                        {
                            RHI_Texture* tex_depth = light->GetDepthTexture();
                            RHI_Texture* tex_color = light->GetShadowsTransparentEnabled() ? light->GetColorTexture() : m_tex_default_white.get();

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

                        // Update light buffer
                        UpdateLightBuffer(cmd_list, light);

                        // Update uber buffer
                        m_buffer_uber_cpu.resolution            = Vector2(static_cast<float>(tex_diffuse->GetWidth()), static_cast<float>(tex_diffuse->GetHeight()));
                        m_buffer_uber_cpu.is_transparent_pass   = is_transparent_pass;
                        UpdateUberBuffer(cmd_list);

                        const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_diffuse->GetWidth()) / m_thread_group_count));
                        const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_diffuse->GetHeight()) / m_thread_group_count));
                        const uint32_t thread_group_count_z = 1;
                        const bool async = false;

                        cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                        cmd_list->EndRenderPass();
                    }
                }
            }
        }
    }

    void Renderer::Pass_Light_Composition(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass /*= false*/)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::Light_Composition_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute   = shader_c;
        pso.pass_name        = is_transparent_pass ? "Pass_Light_Composition_Transparent" : "Pass_Light_Composition_Opaque";

        // Begin commands
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution            = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            m_buffer_uber_cpu.is_transparent_pass   = is_transparent_pass;
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            // Setup command list
            cmd_list->SetTexture(RendererBindingsUav::rgba,             tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_albedo,   RENDER_TARGET(RendererRt::Gbuffer_Albedo));
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal,   RENDER_TARGET(RendererRt::Gbuffer_Normal));
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth,    RENDER_TARGET(RendererRt::Gbuffer_Depth));
            cmd_list->SetTexture(RendererBindingsSrv::light_diffuse,    is_transparent_pass ? RENDER_TARGET(RendererRt::Light_Diffuse_Transparent).get() : RENDER_TARGET(RendererRt::Light_Diffuse).get());
            cmd_list->SetTexture(RendererBindingsSrv::light_specular,   is_transparent_pass ? RENDER_TARGET(RendererRt::Light_Specular_Transparent).get() : RENDER_TARGET(RendererRt::Light_Specular).get());
            cmd_list->SetTexture(RendererBindingsSrv::light_volumetric, RENDER_TARGET(RendererRt::Light_Volumetric));
            cmd_list->SetTexture(RendererBindingsSrv::frame,            RENDER_TARGET(RendererRt::Frame_2)); // refraction
            cmd_list->SetTexture(RendererBindingsSrv::environment,      GetEnvironmentTexture());

            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_Light_ImageBased(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass /*= false*/)
    {
        // The directional light's intensity is used to modulate the environment texture.
        // So, if the intensity is zero, then there is no need to do image based lighting.
        if (m_buffer_frame_cpu.directional_light_intensity == 0.0f)
            return;

        // Acquire shaders
        RHI_Shader* shader_v = m_shaders[RendererShader::Quad_V].get();
        RHI_Shader* shader_p = m_shaders[RendererShader::Light_ImageBased_P].get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        RHI_Texture* tex_depth = RENDER_TARGET(RendererRt::Gbuffer_Depth).get();

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_vertex                           = shader_v;
        pso.shader_pixel                            = shader_p;
        pso.rasterizer_state                        = m_rasterizer_cull_back_solid.get();
        pso.depth_stencil_state                     = is_transparent_pass ? m_depth_stencil_off_r.get() : m_depth_stencil_off_off.get();
        pso.blend_state                             = m_blend_additive.get();
        pso.render_target_color_textures[0]         = tex_out;
        pso.clear_color[0]                          = rhi_color_load;
        pso.render_target_depth_texture             = is_transparent_pass ? tex_depth : nullptr;
        pso.render_target_depth_texture_read_only   = is_transparent_pass;
        pso.clear_depth                             = rhi_depth_load;
        pso.clear_stencil                           = rhi_stencil_load;
        pso.viewport                                = tex_out->GetViewport();
        pso.vertex_buffer_stride                    = m_viewport_quad.GetVertexBuffer()->GetStride();
        pso.primitive_topology                      = RHI_PrimitiveTopology_TriangleList;
        pso.pass_name                               = is_transparent_pass ? "Pass_Light_ImageBased_Transparent" : "Pass_Light_ImageBased_Opaque";

        // Begin commands
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution            = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            m_buffer_uber_cpu.is_transparent_pass   = is_transparent_pass;
            UpdateUberBuffer(cmd_list);

            // Setup command list
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_albedo,   RENDER_TARGET(RendererRt::Gbuffer_Albedo));
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal,   RENDER_TARGET(RendererRt::Gbuffer_Normal));
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_material, RENDER_TARGET(RendererRt::Gbuffer_Material));
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth,    tex_depth);
            cmd_list->SetTexture(RendererBindingsSrv::ssao,             (m_options & Render_Ssao) ? RENDER_TARGET(RendererRt::Ssao_Blurred) : m_tex_default_white);
            cmd_list->SetTexture(RendererBindingsSrv::lutIbl,           RENDER_TARGET(RendererRt::Brdf_Specular_Lut));
            cmd_list->SetTexture(RendererBindingsSrv::environment,      GetEnvironmentTexture());

            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_Blur_Box(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out, const float sigma, const float pixel_stride, const bool use_stencil)
    {
        // Acquire shaders
        const auto& shader_v = m_shaders[RendererShader::Quad_V];
        const auto& shader_p = m_shaders[RendererShader::BlurBox_P];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso         = {};
        pso.shader_vertex                    = shader_v.get();
        pso.shader_pixel                     = shader_p.get();
        pso.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pso.blend_state                      = m_blend_disabled.get();
        pso.depth_stencil_state              = use_stencil ? m_depth_stencil_off_r.get() : m_depth_stencil_off_off.get();
        pso.vertex_buffer_stride             = m_viewport_quad.GetVertexBuffer()->GetStride();
        pso.render_target_color_textures[0]  = tex_out.get();
        pso.clear_color[0]                   = rhi_color_dont_care;
        pso.render_target_depth_texture      = use_stencil ? RENDER_TARGET(RendererRt::Gbuffer_Depth).get() : nullptr;
        pso.viewport                         = tex_out->GetViewport();
        pso.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pso.pass_name                        = "Pass_Blur_Box";

        // Record commands
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution        = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            m_buffer_uber_cpu.blur_direction    = Vector2(pixel_stride, 0.0f);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer(cmd_list);

            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->DrawIndexed(m_viewport_quad.GetIndexCount());
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_Blur_Gaussian(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out, const float sigma, const float pixel_stride)
    {
        if (tex_in->GetWidth() != tex_out->GetWidth() || tex_in->GetHeight() != tex_out->GetHeight() || tex_in->GetFormat() != tex_out->GetFormat())
        {
            LOG_ERROR("Invalid parameters, textures must match because they will get swapped");
            return;
        }

        // Acquire shaders
        const auto& shader_v = m_shaders[RendererShader::Quad_V];
        const auto& shader_p = m_shaders[RendererShader::BlurGaussian_P];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Set render state for horizontal pass
        static RHI_PipelineState pso_horizontal;
        pso_horizontal.shader_vertex                     = shader_v.get();
        pso_horizontal.shader_pixel                      = shader_p.get();
        pso_horizontal.rasterizer_state                  = m_rasterizer_cull_back_solid.get();
        pso_horizontal.blend_state                       = m_blend_disabled.get();
        pso_horizontal.depth_stencil_state               = m_depth_stencil_off_off.get();
        pso_horizontal.vertex_buffer_stride              = m_viewport_quad.GetVertexBuffer()->GetStride();
        pso_horizontal.render_target_color_textures[0]   = tex_out.get();
        pso_horizontal.clear_color[0]                    = rhi_color_dont_care;
        pso_horizontal.viewport                          = tex_out->GetViewport();
        pso_horizontal.primitive_topology                = RHI_PrimitiveTopology_TriangleList;
        pso_horizontal.pass_name                         = "Pass_Blur_Gaussian_Horizontal";

        // Record commands for horizontal pass
        if (cmd_list->BeginRenderPass(pso_horizontal))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution        = Vector2(static_cast<float>(tex_in->GetWidth()), static_cast<float>(tex_in->GetHeight()));
            m_buffer_uber_cpu.blur_direction    = Vector2(pixel_stride, 0.0f);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer(cmd_list);
        
            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->EndRenderPass();
        }
        
        // Set render state for vertical pass
        static RHI_PipelineState pso_vertical;
        pso_vertical.shader_vertex                   = shader_v.get();
        pso_vertical.shader_pixel                    = shader_p.get();
        pso_vertical.rasterizer_state                = m_rasterizer_cull_back_solid.get();
        pso_vertical.blend_state                     = m_blend_disabled.get();
        pso_vertical.depth_stencil_state             = m_depth_stencil_off_off.get();
        pso_vertical.vertex_buffer_stride            = m_viewport_quad.GetVertexBuffer()->GetStride();
        pso_vertical.render_target_color_textures[0] = tex_in.get();
        pso_vertical.clear_color[0]                  = rhi_color_dont_care;
        pso_vertical.viewport                        = tex_in->GetViewport();
        pso_vertical.primitive_topology              = RHI_PrimitiveTopology_TriangleList;
        pso_vertical.pass_name                       = "Pass_Blur_Gaussian_Vertical";

        // Record commands for vertical pass
        if (cmd_list->BeginRenderPass(pso_vertical))
        {
            m_buffer_uber_cpu.blur_direction    = Vector2(0.0f, pixel_stride);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer(cmd_list);
        
            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_out);
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->EndRenderPass();
        }

        // Swap textures
        tex_in.swap(tex_out);
    }

    void Renderer::Pass_Blur_BilateralGaussian(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out, const float sigma, const float pixel_stride, const bool use_stencil)
    {
        if (tex_in->GetWidth() != tex_out->GetWidth() || tex_in->GetHeight() != tex_out->GetHeight() || tex_in->GetFormat() != tex_out->GetFormat())
        {
            LOG_ERROR("Invalid parameters, textures must match because they will get swapped.");
            return;
        }

        // Acquire shaders
        const auto& shader_v = m_shaders[RendererShader::Quad_V];
        const auto& shader_p = m_shaders[RendererShader::BlurGaussianBilateral_P];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Acquire render targets
        RHI_Texture* tex_depth     = RENDER_TARGET(RendererRt::Gbuffer_Depth).get();
        RHI_Texture* tex_normal    = RENDER_TARGET(RendererRt::Gbuffer_Normal).get();

        // Set render state for horizontal pass
        static RHI_PipelineState pso;
        pso.shader_vertex                     = shader_v.get();
        pso.shader_pixel                      = shader_p.get();
        pso.rasterizer_state                  = m_rasterizer_cull_back_solid.get();
        pso.blend_state                       = m_blend_disabled.get();
        pso.depth_stencil_state               = use_stencil ? m_depth_stencil_off_r.get() : m_depth_stencil_off_off.get();
        pso.vertex_buffer_stride              = m_viewport_quad.GetVertexBuffer()->GetStride();
        pso.render_target_color_textures[0]   = tex_out.get();
        pso.clear_color[0]                    = rhi_color_dont_care;
        pso.render_target_depth_texture       = use_stencil ? tex_depth : nullptr;
        pso.clear_stencil                     = use_stencil ? rhi_stencil_load : rhi_stencil_dont_care;
        pso.viewport                          = tex_out->GetViewport();
        pso.primitive_topology                = RHI_PrimitiveTopology_TriangleList;
        pso.pass_name                         = "Pass_Blur_BilateralGaussian_Horizontal";

        // Record commands for horizontal pass
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution        = Vector2(static_cast<float>(tex_in->GetWidth()), static_cast<float>(tex_in->GetHeight()));
            m_buffer_uber_cpu.blur_direction    = Vector2(pixel_stride, 0.0f);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer(cmd_list);

            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal, tex_normal);
            cmd_list->DrawIndexed(m_viewport_quad.GetIndexCount());
            cmd_list->EndRenderPass();
        }

        // Set render state for vertical pass
        pso.render_target_color_textures[0] = tex_in.get();
        pso.viewport                        = tex_in->GetViewport();
        pso.pass_name                       = "Pass_Blur_BilateralGaussian_Vertical";

        // Record commands for vertical pass
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.blur_direction    = Vector2(0.0f, pixel_stride);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer(cmd_list);

            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal, tex_normal);
            cmd_list->DrawIndexed(m_viewport_quad.GetIndexCount());
            cmd_list->EndRenderPass();
        }

        // Swap textures
        tex_in.swap(tex_out);
    }

    void Renderer::Pass_PostProcess(RHI_CommandList* cmd_list)
    {
        // IN:  RenderTarget_Composition_Hdr
        // OUT: RenderTarget_Composition_Ldr

        // Acquire render targets
        shared_ptr<RHI_Texture>& rt_frame       = RENDER_TARGET(RendererRt::Frame);                 // render res
        shared_ptr<RHI_Texture>& rt_frame_2     = RENDER_TARGET(RendererRt::Frame_2);               // render res
        shared_ptr<RHI_Texture>& rt_frame_pp    = RENDER_TARGET(RendererRt::Frame_PostProcess);     // output res
        shared_ptr<RHI_Texture>& rt_frame_pp_2  = RENDER_TARGET(RendererRt::Frame_PostProcess_2);   // output res

        // Depth of Field
        if (GetOption(Render_DepthOfField))
        {
            Pass_PostProcess_DepthOfField(cmd_list, rt_frame, rt_frame_2);
            rt_frame.swap(rt_frame_2);
        }

        // TAA
        bool copy_required = true;
        if (GetOption(Render_AntiAliasing_Taa))
        {
            if (GetOptionValue<bool>(Renderer_Option_Value::Taa_AllowUpsampling))
            {
                Pass_PostProcess_TAA(cmd_list, rt_frame, rt_frame_pp);
                copy_required = false; // taa writes directly in the high res buffer
            }
            else
            {
                Pass_PostProcess_TAA(cmd_list, rt_frame, rt_frame_2);
                rt_frame.swap(rt_frame_2);
            }
        }

        // FXAA
        if (GetOption(Render_AntiAliasing_Fxaa))
        {
            Pass_PostProcess_Fxaa(cmd_list, rt_frame, rt_frame_2);
            rt_frame.swap(rt_frame_2);
        }

        // If upsampling is disabled but the output resolution is different, do bilinear scaling
        if (copy_required)
        {
            Pass_CopyBilinear(cmd_list, rt_frame.get(), rt_frame_pp.get());
        }

        // Motion Blur
        if (GetOption(Render_MotionBlur))
        {
            Pass_PostProcess_MotionBlur(cmd_list, rt_frame_pp, rt_frame_pp_2);
            rt_frame_pp.swap(rt_frame_pp_2);
        }

        // Bloom
        if (GetOption(Render_Bloom))
        {
            Pass_PostProcess_Bloom(cmd_list, rt_frame_pp, rt_frame_pp_2);
            rt_frame_pp.swap(rt_frame_pp_2);
        }

        // Tone-Mapping
        if (m_option_values[Renderer_Option_Value::Tonemapping] != 0)
        {
            Pass_PostProcess_ToneMapping(cmd_list, rt_frame_pp, rt_frame_pp_2);
            rt_frame_pp.swap(rt_frame_pp_2);
        }

        // Dithering
        if (GetOption(Render_Dithering))
        {
            Pass_PostProcess_Dithering(cmd_list, rt_frame_pp, rt_frame_pp_2);
            rt_frame_pp.swap(rt_frame_pp_2);
        }

        // Sharpening
        if (GetOption(Render_Sharpening_LumaSharpen))
        {
            Pass_PostProcess_Sharpening(cmd_list, rt_frame_pp, rt_frame_pp_2);
            rt_frame_pp.swap(rt_frame_pp_2);
        }

        // Film grain
        if (GetOption(Render_FilmGrain))
        {
            Pass_PostProcess_FilmGrain(cmd_list, rt_frame_pp, rt_frame_pp_2);
            rt_frame_pp.swap(rt_frame_pp_2);
        }

        // Chromatic aberration
        if (GetOption(Render_ChromaticAberration))
        {
            Pass_PostProcess_ChromaticAberration(cmd_list, rt_frame_pp, rt_frame_pp_2);
            rt_frame_pp.swap(rt_frame_pp_2);
        }

        // Gamma correction
        Pass_PostProcess_GammaCorrection(cmd_list, rt_frame_pp, rt_frame_pp_2);

        // Passes that render on top of each other
        Pass_Outline(cmd_list, rt_frame_pp_2.get());
        Pass_TransformHandle(cmd_list, rt_frame_pp_2.get());
        Pass_Lines(cmd_list, rt_frame_pp_2.get());
        Pass_Icons(cmd_list, rt_frame_pp_2.get());
        Pass_DebugBuffer(cmd_list, rt_frame_pp_2.get());
        Pass_Text(cmd_list, rt_frame_pp_2.get());

        // Swap textures
        rt_frame_pp.swap(rt_frame_pp_2);
    }

    void Renderer::Pass_PostProcess_TAA(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::Taa_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Acquire history texture
        RHI_Texture* tex_history = RENDER_TARGET(RendererRt::Taa_History).get();

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute   = shader_c;
        pso.pass_name        = "Pass_PostProcess_TAA";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsSrv::tex,              tex_history);
            cmd_list->SetTexture(RendererBindingsSrv::tex2,             tex_in);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_velocity, RENDER_TARGET(RendererRt::Gbuffer_Velocity));
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth,    RENDER_TARGET(RendererRt::Gbuffer_Depth));
            cmd_list->SetTexture(RendererBindingsUav::rgba,             tex_out);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }

        // Update history buffer
        Pass_Copy(cmd_list, tex_out.get(), tex_history);
    }

    void Renderer::Pass_PostProcess_Bloom(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_downsampleLuminance  = m_shaders[RendererShader::BloomDownsampleLuminance_C].get();
        RHI_Shader* shader_downsample           = m_shaders[RendererShader::BloomDownsample_C].get();
        RHI_Shader* shader_upsampleBlendMip     = m_shaders[RendererShader::BloomUpsampleBlendMip_C].get();
        RHI_Shader* shader_upsampleBlendFrame   = m_shaders[RendererShader::BloomUpsampleBlendFrame_C].get();
        if (!shader_downsampleLuminance->IsCompiled() || !shader_upsampleBlendMip->IsCompiled() || !shader_downsample->IsCompiled() || !shader_upsampleBlendFrame->IsCompiled())
            return;

        // Luminance
        {
            // Set render state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_downsampleLuminance;
            pso.pass_name      = "Pass_PostProcess_BloomDownsampleLuminance";

            // Draw
            if (cmd_list->BeginRenderPass(pso))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(m_render_tex_bloom[0].get()->GetWidth()), static_cast<float>(m_render_tex_bloom[0].get()->GetHeight()));
                UpdateUberBuffer(cmd_list);

                const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(m_render_tex_bloom[0].get()->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(m_render_tex_bloom[0].get()->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z   = 1;
                const bool async                      = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, m_render_tex_bloom[0].get());
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }
        
        // Downsample
        // The last bloom texture is the same size as the previous one (it's used for the Gaussian pass below), so we skip it
        for (int i = 0; i < static_cast<int>(m_render_tex_bloom.size() - 1); i++)
        {
            RHI_Texture* mip_small = m_render_tex_bloom[i + 1].get();
            RHI_Texture* mip_large = m_render_tex_bloom[i].get();

            // Set render state
            static RHI_PipelineState pso;
            pso.shader_compute   = shader_downsample;
            pso.pass_name        = "Pass_PostProcess_BloomDownsample";

            // Draw
            if (cmd_list->BeginRenderPass(pso))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(mip_small->GetWidth()), static_cast<float>(mip_small->GetHeight()));
                UpdateUberBuffer(cmd_list);

                const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(mip_small->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(mip_small->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z = 1;
                const bool async = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, mip_small);
                cmd_list->SetTexture(RendererBindingsSrv::tex, mip_large);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }
        
        // Starting from the smallest mip, upsample and blend with the higher one
        for (int i = static_cast<int>(m_render_tex_bloom.size() - 1); i > 0; i--)
        {
            RHI_Texture* mip_small = m_render_tex_bloom[i].get();
            RHI_Texture* mip_large = m_render_tex_bloom[i - 1].get();

            // Set render state
            static RHI_PipelineState pso;
            pso.shader_compute   = shader_upsampleBlendMip;
            pso.pass_name        = "Pass_PostProcess_BloomUpsampleBlendMip";

            // Draw
            if (cmd_list->BeginRenderPass(pso))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(mip_large->GetWidth()), static_cast<float>(mip_large->GetHeight()));
                UpdateUberBuffer(cmd_list);

                const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(mip_large->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(mip_large->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z = 1;
                const bool async = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, mip_large);
                cmd_list->SetTexture(RendererBindingsSrv::tex, mip_small);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }
        
        // Upsample mip 2 and blend with mip 1, then blend with the frame
        {
            // Set render state
            static RHI_PipelineState pso;
            pso.shader_compute   = shader_upsampleBlendFrame;
            pso.pass_name        = "Pass_PostProcess_BloomUpsampleBlendFrame";

            // Draw
            if (cmd_list->BeginRenderPass(pso))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
                UpdateUberBuffer(cmd_list);

                const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z = 1;
                const bool async = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out.get());
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
                cmd_list->SetTexture(RendererBindingsSrv::tex2, m_render_tex_bloom.front());
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }
    }

    void Renderer::Pass_PostProcess_ToneMapping(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::ToneMapping_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        pso.pass_name      = "Pass_PostProcess_ToneMapping";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_PostProcess_GammaCorrection(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::GammaCorrection_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute   = shader_c;
        pso.pass_name        = "Pass_PostProcess_GammaCorrection";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z   = 1;
            const bool async                      = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_PostProcess_Fxaa(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_p_luma   = m_shaders[RendererShader::Fxaa_Luminance_C].get();
        RHI_Shader* shader_p_fxaa   = m_shaders[RendererShader::Fxaa_C].get();
        if (!shader_p_luma->IsCompiled() || !shader_p_fxaa->IsCompiled())
            return;

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer(cmd_list);

        // Compute thread count
        const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
        const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
        const uint32_t thread_group_count_z = 1;
        const bool async = false;

        // Luminance
        {
            // Set render state
            static RHI_PipelineState pso;
            pso.shader_compute   = shader_p_luma;
            pso.pass_name        = "Pass_PostProcess_FXAA_Luminance";

            // Draw
            if (cmd_list->BeginRenderPass(pso))
            {
                cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }

        // FXAA
        {
            // Set render state
            static RHI_PipelineState pso;
            pso.shader_compute   = shader_p_fxaa;
            pso.pass_name        = "Pass_PostProcess_FXAA";

            // Draw
            if (cmd_list->BeginRenderPass(pso))
            {
                cmd_list->SetTexture(RendererBindingsUav::rgba, tex_in);
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_out);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }

        // Swap the textures
        tex_in.swap(tex_out);
    }

    void Renderer::Pass_PostProcess_ChromaticAberration(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::ChromaticAberration_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        pso.pass_name      = "Pass_PostProcess_ChromaticAberration";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z   = 1;
            const bool async                      = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_PostProcess_MotionBlur(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::MotionBlur_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        pso.pass_name      = "Pass_PostProcess_MotionBlur";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z   = 1;
            const bool async                      = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_velocity, RENDER_TARGET(RendererRt::Gbuffer_Velocity));
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth,    RENDER_TARGET(RendererRt::Gbuffer_Depth));
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_PostProcess_DepthOfField(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_downsampleCoc    = m_shaders[RendererShader::Dof_DownsampleCoc_C].get();
        RHI_Shader* shader_bokeh            = m_shaders[RendererShader::Dof_Bokeh_C].get();
        RHI_Shader* shader_tent             = m_shaders[RendererShader::Dof_Tent_C].get();
        RHI_Shader* shader_upsampleBlend    = m_shaders[RendererShader::Dof_UpscaleBlend_C].get();
        if (!shader_downsampleCoc->IsCompiled() || !shader_bokeh->IsCompiled() || !shader_tent->IsCompiled() || !shader_upsampleBlend->IsCompiled())
            return;

        // Acquire render targets
        RHI_Texture* tex_bokeh_half     = RENDER_TARGET(RendererRt::Dof_Half).get();
        RHI_Texture* tex_bokeh_half_2   = RENDER_TARGET(RendererRt::Dof_Half_2).get();
        RHI_Texture* tex_depth          = RENDER_TARGET(RendererRt::Gbuffer_Depth).get();

        // Downsample and compute circle of confusion
        {
            // Set render state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_downsampleCoc;
            pso.pass_name      = "Pass_PostProcess_Dof_DownsampleCoc";

            // Draw
            if (cmd_list->BeginRenderPass(pso))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_bokeh_half->GetWidth()), static_cast<float>(tex_bokeh_half->GetHeight()));
                UpdateUberBuffer(cmd_list);

                const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z   = 1;
                const bool async                      = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, tex_bokeh_half);
                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }

        // Bokeh
        {
            // Set render state
            static RHI_PipelineState pso;
            pso.shader_compute   = shader_bokeh;
            pso.pass_name        = "Pass_PostProcess_Dof_Bokeh";

            // Draw
            if (cmd_list->BeginRenderPass(pso))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_bokeh_half_2->GetWidth()), static_cast<float>(tex_bokeh_half_2->GetHeight()));
                UpdateUberBuffer(cmd_list);

                const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half_2->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half_2->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z = 1;
                const bool async = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, tex_bokeh_half_2);
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_bokeh_half);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }

        // Blur the bokeh using a tent filter
        {
            // Set render state
            static RHI_PipelineState pso;
            pso.shader_compute   = shader_tent;
            pso.pass_name        = "Pass_PostProcess_Dof_Tent";

            // Draw
            if (cmd_list->BeginRenderPass(pso))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_bokeh_half->GetWidth()), static_cast<float>(tex_bokeh_half->GetHeight()));
                UpdateUberBuffer(cmd_list);

                const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z = 1;
                const bool async = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, tex_bokeh_half);
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_bokeh_half_2);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }

        // Upscale & Blend
        {
            // Set render state
            static RHI_PipelineState pso;
            pso.shader_compute   = shader_upsampleBlend;
            pso.pass_name        = "Pass_PostProcess_Dof_UpscaleBlend";

            // Draw
            if (cmd_list->BeginRenderPass(pso))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
                UpdateUberBuffer(cmd_list);

                const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z = 1;
                const bool async = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
                cmd_list->SetTexture(RendererBindingsSrv::tex2, tex_bokeh_half);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }
    }

    void Renderer::Pass_PostProcess_Dithering(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader = m_shaders[RendererShader::Dithering_C].get();
        if (!shader->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute   = shader;
        pso.pass_name        = "Pass_PostProcess_Dithering";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_PostProcess_FilmGrain(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::FilmGrain_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        pso.pass_name      = "Pass_PostProcess_FilmGrain";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z   = 1;
            const bool async                      = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_PostProcess_Sharpening(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::Sharpening_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        pso.pass_name      = "Pass_PostProcess_Sharpening";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z   = 1;
            const bool async                      = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_Lines(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        const bool draw_picking_ray = m_options & Render_Debug_PickingRay;
        const bool draw_aabb        = m_options & Render_Debug_Aabb;
        const bool draw_grid        = m_options & Render_Debug_Grid;
        const bool draw_lights      = m_options & Render_Debug_Lights;
        const bool draw_lines       = !m_lines_depth_disabled.empty() || !m_lines_depth_enabled.empty(); // Any kind of lines, physics, user debug, etc.
        const bool draw             = draw_picking_ray || draw_aabb || draw_grid || draw_lines || draw_lights;
        if (!draw)
            return;

        // Acquire color shaders
        RHI_Shader* shader_color_v = m_shaders[RendererShader::Color_V].get();
        RHI_Shader* shader_color_p = m_shaders[RendererShader::Color_P].get();
        if (!shader_color_v->IsCompiled() || !shader_color_p->IsCompiled())
            return;

        // Grid
        if (draw_grid)
        {
            // Set render state
            static RHI_PipelineState pso;
            pso.shader_vertex                    = shader_color_v;
            pso.shader_pixel                     = shader_color_p;
            pso.rasterizer_state                 = m_rasterizer_cull_back_wireframe.get();
            pso.blend_state                      = m_blend_alpha.get();
            pso.depth_stencil_state              = m_depth_stencil_r_off.get();
            pso.vertex_buffer_stride             = m_gizmo_grid->GetVertexBuffer()->GetStride();
            pso.render_target_color_textures[0]  = tex_out;
            pso.render_target_depth_texture      = RENDER_TARGET(RendererRt::Gbuffer_Depth).get();
            pso.viewport                         = tex_out->GetViewport();
            pso.primitive_topology               = RHI_PrimitiveTopology_LineList;
            pso.pass_name                        = "Pass_Lines_Grid";
        
            // Create and submit command list
            if (cmd_list->BeginRenderPass(pso))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution    = m_resolution_render;
                m_buffer_uber_cpu.transform     = m_gizmo_grid->ComputeWorldMatrix(m_camera->GetTransform()) * m_buffer_frame_cpu.view_projection_unjittered;
                UpdateUberBuffer(cmd_list);
        
                cmd_list->SetBufferIndex(m_gizmo_grid->GetIndexBuffer().get());
                cmd_list->SetBufferVertex(m_gizmo_grid->GetVertexBuffer().get());
                cmd_list->DrawIndexed(m_gizmo_grid->GetIndexCount());
                cmd_list->EndRenderPass();
            }
        }

        // Generate lines for debug primitives supported by the renderer
        {
            // Picking ray
            if (draw_picking_ray)
            {
                const auto& ray = m_camera->GetPickingRay();
                DrawLine(ray.GetStart(), ray.GetStart() + ray.GetDirection() * m_camera->GetFarPlane(), Vector4(0, 1, 0, 1));
            }

            // Lights
            if (draw_lights)
            {
                auto& lights = m_entities[Renderer_Object_Light];
                for (const auto& entity : lights)
                {
                    Light* light = entity->GetComponent<Light>();

                    if (light->GetLightType() == LightType::Spot)
                    {
                        // tan(angle) = opposite/adjacent
                        // opposite = adjacent * tan(angle)
                        float opposite  = light->GetRange() * Math::Helper::Tan(light->GetAngle());

                        Vector3 pos_end_center  = light->GetTransform()->GetForward() * light->GetRange();
                        Vector3 pos_end_up      = pos_end_center + light->GetTransform()->GetUp()      * opposite;
                        Vector3 pos_end_right   = pos_end_center + light->GetTransform()->GetRight()   * opposite;
                        Vector3 pos_end_down    = pos_end_center + light->GetTransform()->GetDown()    * opposite;
                        Vector3 pos_end_left    = pos_end_center + light->GetTransform()->GetLeft()    * opposite;

                        Vector3 pos_start = light->GetTransform()->GetPosition();
                        DrawLine(pos_start, pos_start + pos_end_center, Vector4(0, 1, 0, 1));
                        DrawLine(pos_start, pos_start + pos_end_up    , Vector4(0, 0, 1, 1));
                        DrawLine(pos_start, pos_start + pos_end_right , Vector4(0, 0, 1, 1));
                        DrawLine(pos_start, pos_start + pos_end_down  , Vector4(0, 0, 1, 1));
                        DrawLine(pos_start, pos_start + pos_end_left  , Vector4(0, 0, 1, 1));
                    }
                }
            }

            // AABBs
            if (draw_aabb)
            {
                for (const auto& entity : m_entities[Renderer_Object_Opaque])
                {
                    if (auto renderable = entity->GetRenderable())
                    {
                        DrawBox(renderable->GetAabb(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
                    }
                }

                for (const auto& entity : m_entities[Renderer_Object_Transparent])
                {
                    if (auto renderable = entity->GetRenderable())
                    {
                        DrawBox(renderable->GetAabb(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
                    }
                }
            }
        }

        // Draw lines
        {
            // Width depth
            uint32_t line_vertex_buffer_size = static_cast<uint32_t>(m_lines_depth_enabled.size());
            if (line_vertex_buffer_size != 0)
            {
                // Grow vertex buffer (if needed)
                if (line_vertex_buffer_size > m_vertex_buffer_lines->GetVertexCount())
                {
                    m_vertex_buffer_lines->CreateDynamic<RHI_Vertex_PosCol>(line_vertex_buffer_size);
                }

                // Update vertex buffer
                RHI_Vertex_PosCol* buffer = static_cast<RHI_Vertex_PosCol*>(m_vertex_buffer_lines->Map());
                std::copy(m_lines_depth_enabled.begin(), m_lines_depth_enabled.end(), buffer);
                m_vertex_buffer_lines->Unmap();

                // Set render state
                static RHI_PipelineState pso;
                pso.shader_vertex                    = shader_color_v;
                pso.shader_pixel                     = shader_color_p;
                pso.rasterizer_state                 = m_rasterizer_cull_back_wireframe.get();
                pso.blend_state                      = m_blend_alpha.get();
                pso.depth_stencil_state              = m_depth_stencil_r_off.get();
                pso.vertex_buffer_stride             = m_vertex_buffer_lines->GetStride();
                pso.render_target_color_textures[0]  = tex_out;
                pso.render_target_depth_texture      = RENDER_TARGET(RendererRt::Gbuffer_Depth).get();
                pso.viewport                         = tex_out->GetViewport();
                pso.primitive_topology               = RHI_PrimitiveTopology_LineList;
                pso.pass_name                        = "Pass_Lines";

                // Create and submit command list
                if (cmd_list->BeginRenderPass(pso))
                {
                    cmd_list->SetBufferVertex(m_vertex_buffer_lines.get());
                    cmd_list->Draw(line_vertex_buffer_size);
                    cmd_list->EndRenderPass();
                }
            }

            // Without depth
            line_vertex_buffer_size = static_cast<uint32_t>(m_lines_depth_disabled.size());
            if (line_vertex_buffer_size != 0)
            {
                // Grow vertex buffer (if needed)
                if (line_vertex_buffer_size > m_vertex_buffer_lines->GetVertexCount())
                {
                    m_vertex_buffer_lines->CreateDynamic<RHI_Vertex_PosCol>(line_vertex_buffer_size);
                }

                // Update vertex buffer
                RHI_Vertex_PosCol* buffer = static_cast<RHI_Vertex_PosCol*>(m_vertex_buffer_lines->Map());
                std::copy(m_lines_depth_disabled.begin(), m_lines_depth_disabled.end(), buffer);
                m_vertex_buffer_lines->Unmap();

                // Set render state
                static RHI_PipelineState pso;
                pso.shader_vertex                    = shader_color_v;
                pso.shader_pixel                     = shader_color_p;
                pso.rasterizer_state                 = m_rasterizer_cull_back_wireframe.get();
                pso.blend_state                      = m_blend_disabled.get();
                pso.depth_stencil_state              = m_depth_stencil_off_off.get();
                pso.vertex_buffer_stride             = m_vertex_buffer_lines->GetStride();
                pso.render_target_color_textures[0]  = tex_out;
                pso.viewport                         = tex_out->GetViewport();
                pso.primitive_topology               = RHI_PrimitiveTopology_LineList;
                pso.pass_name                        = "Pass_Lines_No_Depth";

                // Create and submit command list
                if (cmd_list->BeginRenderPass(pso))
                {
                    cmd_list->SetBufferVertex(m_vertex_buffer_lines.get());
                    cmd_list->Draw(line_vertex_buffer_size);
                    cmd_list->EndRenderPass();
                }
            }
        }
    }

    void Renderer::Pass_Icons(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (!(m_options & Render_Debug_Lights))
            return;

        // Acquire resources
        auto& lights                    = m_entities[Renderer_Object_Light];
        const auto& shader_quad_v       = m_shaders[RendererShader::Quad_V];
        const auto& shader_texture_p    = m_shaders[RendererShader::Copy_Bilinear_P];
        if (lights.empty() || !shader_quad_v->IsCompiled() || !shader_texture_p->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_vertex                    = shader_quad_v.get();
        pso.shader_pixel                     = shader_texture_p.get();
        pso.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pso.blend_state                      = m_blend_alpha.get();
        pso.depth_stencil_state              = m_depth_stencil_off_off.get();
        pso.vertex_buffer_stride             = m_viewport_quad.GetVertexBuffer()->GetStride(); // stride matches rect
        pso.render_target_color_textures[0]  = tex_out;
        pso.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pso.viewport                         = tex_out->GetViewport();
        pso.pass_name                        = "Pass_Icons";

        // For each light
        for (const auto& entity : lights)
        {
            if (cmd_list->BeginRenderPass(pso))
            {
                // Light can be null if it just got removed and our buffer doesn't update till the next frame
                if (Light* light = entity->GetComponent<Light>())
                {
                    Vector3 position_light_world        = entity->GetTransform()->GetPosition();
                    Vector3 position_camera_world       = m_camera->GetTransform()->GetPosition();
                    Vector3 direction_camera_to_light   = (position_light_world - position_camera_world).Normalized();
                    const float v_dot_l                 = Vector3::Dot(m_camera->GetTransform()->GetForward(), direction_camera_to_light);

                    // Only draw if it's inside our view
                    if (v_dot_l > 0.5f)
                    {
                        // Compute light screen space position and scale (based on distance from the camera)
                        const Vector2 position_light_screen = m_camera->Project(position_light_world);
                        const float distance                = (position_camera_world - position_light_world).Length() + Helper::EPSILON;
                        float scale                         = m_gizmo_size_max / distance;
                        scale                               = Helper::Clamp(scale, m_gizmo_size_min, m_gizmo_size_max);

                        // Choose texture based on light type
                        shared_ptr<RHI_Texture> light_tex = nullptr;
                        const LightType type = light->GetLightType();
                        if (type == LightType::Directional) light_tex = m_tex_gizmo_light_directional;
                        else if (type == LightType::Point)  light_tex = m_tex_gizmo_light_point;
                        else if (type == LightType::Spot)   light_tex = m_tex_gizmo_light_spot;

                        // Construct appropriate rectangle
                        const float tex_width = light_tex->GetWidth() * scale;
                        const float tex_height = light_tex->GetHeight() * scale;
                        Math::Rectangle rectangle = Math::Rectangle
                        (
                            position_light_screen.x - tex_width * 0.5f,
                            position_light_screen.y - tex_height * 0.5f,
                            position_light_screen.x + tex_width,
                            position_light_screen.y + tex_height
                        );

                        if (rectangle != m_gizmo_light_rect)
                        {
                            m_gizmo_light_rect = rectangle;
                            m_gizmo_light_rect.CreateBuffers(this);
                        }

                        // Update uber buffer
                        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_width), static_cast<float>(tex_width));
                        m_buffer_uber_cpu.transform = m_buffer_frame_cpu.view_projection_ortho;
                        UpdateUberBuffer(cmd_list);

                        cmd_list->SetTexture(RendererBindingsSrv::tex, light_tex);
                        cmd_list->SetBufferIndex(m_gizmo_light_rect.GetIndexBuffer());
                        cmd_list->SetBufferVertex(m_gizmo_light_rect.GetVertexBuffer());
                        cmd_list->DrawIndexed(Rectangle::GetIndexCount());
                    }
                }
                cmd_list->EndRenderPass();
            }
        }
    }

    void Renderer::Pass_TransformHandle(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (!GetOption(Render_Debug_Transform))
            return;

        // Acquire resources
        RHI_Shader* shader_gizmo_transform_v    = m_shaders[RendererShader::Entity_V].get();
        RHI_Shader* shader_gizmo_transform_p    = m_shaders[RendererShader::Entity_Transform_P].get();
        if (!shader_gizmo_transform_v->IsCompiled() || !shader_gizmo_transform_p->IsCompiled())
            return;

        // Transform
        if (m_transform_handle->Tick(m_camera.get(), m_gizmo_transform_size, m_gizmo_transform_speed))
        {
            // Set render state
            static RHI_PipelineState pso;
            pso.shader_vertex                    = shader_gizmo_transform_v;
            pso.shader_pixel                     = shader_gizmo_transform_p;
            pso.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
            pso.blend_state                      = m_blend_alpha.get();
            pso.depth_stencil_state              = m_depth_stencil_off_off.get();
            pso.vertex_buffer_stride             = m_transform_handle->GetVertexBuffer()->GetStride();
            pso.render_target_color_textures[0]  = tex_out;
            pso.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
            pso.viewport                         = tex_out->GetViewport();

            // Axis - X
            pso.pass_name = "Pass_Handle_Axis_X";
            if (cmd_list->BeginRenderPass(pso))
            {
                m_buffer_uber_cpu.transform         = m_transform_handle->GetHandle()->GetTransform(Vector3::Right);
                m_buffer_uber_cpu.transform_axis    = m_transform_handle->GetHandle()->GetColor(Vector3::Right);
                UpdateUberBuffer(cmd_list);
            
                cmd_list->SetBufferIndex(m_transform_handle->GetIndexBuffer());
                cmd_list->SetBufferVertex(m_transform_handle->GetVertexBuffer());
                cmd_list->DrawIndexed(m_transform_handle->GetIndexCount());
                cmd_list->EndRenderPass();
            }
            
            // Axis - Y
            pso.pass_name = "Pass_Handle_Axis_Y";
            if (cmd_list->BeginRenderPass(pso))
            {
                m_buffer_uber_cpu.transform         = m_transform_handle->GetHandle()->GetTransform(Vector3::Up);
                m_buffer_uber_cpu.transform_axis    = m_transform_handle->GetHandle()->GetColor(Vector3::Up);
                UpdateUberBuffer(cmd_list);

                cmd_list->SetBufferIndex(m_transform_handle->GetIndexBuffer());
                cmd_list->SetBufferVertex(m_transform_handle->GetVertexBuffer());
                cmd_list->DrawIndexed(m_transform_handle->GetIndexCount());
                cmd_list->EndRenderPass();
            }
            
            // Axis - Z
            pso.pass_name = "Pass_Handle_Axis_Z";
            if (cmd_list->BeginRenderPass(pso))
            {
                m_buffer_uber_cpu.transform         = m_transform_handle->GetHandle()->GetTransform(Vector3::Forward);
                m_buffer_uber_cpu.transform_axis    = m_transform_handle->GetHandle()->GetColor(Vector3::Forward);
                UpdateUberBuffer(cmd_list);

                cmd_list->SetBufferIndex(m_transform_handle->GetIndexBuffer());
                cmd_list->SetBufferVertex(m_transform_handle->GetVertexBuffer());
                cmd_list->DrawIndexed(m_transform_handle->GetIndexCount());
                cmd_list->EndRenderPass();
            }
            
            // Axes - XYZ
            if (m_transform_handle->DrawXYZ())
            {
                pso.pass_name = "Pass_Gizmos_Axis_XYZ";
                if (cmd_list->BeginRenderPass(pso))
                {
                    m_buffer_uber_cpu.transform         = m_transform_handle->GetHandle()->GetTransform(Vector3::One);
                    m_buffer_uber_cpu.transform_axis    = m_transform_handle->GetHandle()->GetColor(Vector3::One);
                    UpdateUberBuffer(cmd_list);

                    cmd_list->SetBufferIndex(m_transform_handle->GetIndexBuffer());
                    cmd_list->SetBufferVertex(m_transform_handle->GetVertexBuffer());
                    cmd_list->DrawIndexed(m_transform_handle->GetIndexCount());
                    cmd_list->EndRenderPass();
                }
            }
        }
    }

    void Renderer::Pass_Outline(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (!GetOption(Render_Debug_SelectionOutline))
            return;

        if (const Entity* entity = m_transform_handle->GetSelectedEntity())
        {
            // Get renderable
            const Renderable* renderable = entity->GetRenderable();
            if (!renderable)
                return;

            // Get material
            const Material* material = renderable->GetMaterial();
            if (!material)
                return;

            // Get geometry
            const Model* model = renderable->GeometryModel();
            if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
                return;

            // Acquire shaders
            const auto& shader_v = m_shaders[RendererShader::Entity_V];
            const auto& shader_p = m_shaders[RendererShader::Entity_Outline_P];
            if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
                return;

            RHI_Texture* tex_depth  = RENDER_TARGET(RendererRt::Gbuffer_Depth).get();
            RHI_Texture* tex_normal = RENDER_TARGET(RendererRt::Gbuffer_Normal).get();

            // Set render state
            static RHI_PipelineState pso;
            pso.shader_vertex                            = shader_v.get();
            pso.shader_pixel                             = shader_p.get();
            pso.rasterizer_state                         = m_rasterizer_cull_back_solid.get();
            pso.blend_state                              = m_blend_alpha.get();
            pso.depth_stencil_state                      = m_depth_stencil_r_off.get();
            pso.vertex_buffer_stride                     = model->GetVertexBuffer()->GetStride();
            pso.render_target_color_textures[0]          = tex_out;
            pso.render_target_depth_texture              = tex_depth;
            pso.render_target_depth_texture_read_only    = true;
            pso.primitive_topology                       = RHI_PrimitiveTopology_TriangleList;
            pso.viewport                                 = tex_out->GetViewport();
            pso.pass_name                                = "Pass_Outline";

            // Record commands
            if (cmd_list->BeginRenderPass(pso))
            {
                 // Update uber buffer with entity transform
                if (Transform* transform = entity->GetTransform())
                {
                    m_buffer_uber_cpu.transform     = transform->GetMatrix();
                    m_buffer_uber_cpu.resolution    = Vector2(tex_out->GetWidth(), tex_out->GetHeight());
                    UpdateUberBuffer(cmd_list);
                }

                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal, tex_normal);
                cmd_list->SetBufferVertex(model->GetVertexBuffer());
                cmd_list->SetBufferIndex(model->GetIndexBuffer());
                cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
                cmd_list->EndRenderPass();
            }
        }
    }

    void Renderer::Pass_Text(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        // Early exit cases
        const bool draw         = m_options & Render_Debug_PerformanceMetrics;
        const bool empty        = m_profiler->GetMetrics().empty();
        const auto& shader_v    = m_shaders[RendererShader::Font_V];
        const auto& shader_p    = m_shaders[RendererShader::Font_P];
        if (!draw || empty || !shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_vertex                    = shader_v.get();
        pso.shader_pixel                     = shader_p.get();
        pso.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pso.blend_state                      = m_blend_alpha.get();
        pso.depth_stencil_state              = m_depth_stencil_off_off.get();
        pso.vertex_buffer_stride             = m_font->GetVertexBuffer()->GetStride();
        pso.render_target_color_textures[0]  = tex_out;
        pso.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pso.viewport                         = tex_out->GetViewport();
        pso.pass_name                        = "Pass_Text";

        // Update text
        const Vector2 text_pos = Vector2(-m_viewport.width * 0.5f + 5.0f, m_viewport.height * 0.5f - m_font->GetSize() - 2.0f);
        m_font->SetText(m_profiler->GetMetrics(), text_pos);

        // Draw outline
        if (m_font->GetOutline() != Font_Outline_None && m_font->GetOutlineSize() != 0)
        { 
            if (cmd_list->BeginRenderPass(pso))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution    = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
                m_buffer_uber_cpu.color         = m_font->GetColorOutline();
                UpdateUberBuffer(cmd_list);

                cmd_list->SetBufferIndex(m_font->GetIndexBuffer());
                cmd_list->SetBufferVertex(m_font->GetVertexBuffer());
                cmd_list->SetTexture(RendererBindingsSrv::font_atlas, m_font->GetAtlasOutline());
                cmd_list->DrawIndexed(m_font->GetIndexCount());
                cmd_list->EndRenderPass();
            }
        }

        // Draw 
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution    = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            m_buffer_uber_cpu.color         = m_font->GetColor();
            UpdateUberBuffer(cmd_list);

            cmd_list->SetBufferIndex(m_font->GetIndexBuffer());
            cmd_list->SetBufferVertex(m_font->GetVertexBuffer());
            cmd_list->SetTexture(RendererBindingsSrv::font_atlas, m_font->GetAtlas());
            cmd_list->DrawIndexed(m_font->GetIndexCount());
            cmd_list->EndRenderPass();
        }
    }

    bool Renderer::Pass_DebugBuffer(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (m_render_target_debug == RendererRt::Undefined)
            return true;

        // Bind correct texture & shader pass
        RHI_Texture* texture          = RENDER_TARGET(m_render_target_debug).get();
        RendererShader shader_type    = RendererShader::Copy_Point_C;

        if (m_render_target_debug == RendererRt::Gbuffer_Albedo)
        {
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == RendererRt::Gbuffer_Normal)
        {
            shader_type = RendererShader::DebugNormal_C;
        }

        if (m_render_target_debug == RendererRt::Gbuffer_Material)
        {
            shader_type = RendererShader::Copy_Point_C;
        }

        if (m_render_target_debug == RendererRt::Light_Diffuse || m_render_target_debug == RendererRt::Light_Diffuse_Transparent)
        {
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == RendererRt::Light_Specular || m_render_target_debug == RendererRt::Light_Specular_Transparent)
        {
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == RendererRt::Gbuffer_Velocity)
        {
            shader_type = RendererShader::DebugVelocity_C;
        }

        if (m_render_target_debug == RendererRt::Gbuffer_Depth)
        {
            shader_type = RendererShader::DebugChannelR_C;
        }

        if (m_render_target_debug == RendererRt::Ssao_Blurred)
        {
            texture     = m_options & Render_Ssao ? RENDER_TARGET(RendererRt::Ssao_Blurred).get() : m_tex_default_white.get();
            shader_type = RendererShader::DebugChannelR_C;
        }

        if (m_render_target_debug == RendererRt::Ssao)
        {
            texture = m_options & Render_Ssao ? RENDER_TARGET(RendererRt::Ssao).get() : m_tex_default_white.get();
            shader_type = RendererShader::DebugChannelR_C;
        }

        if (m_render_target_debug == RendererRt::Ssgi_Raw)
        {
            texture = m_options & Render_Ssgi ? RENDER_TARGET(RendererRt::Ssgi_Raw).get() : m_tex_default_black.get();
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == RendererRt::Ssr)
        {
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == RendererRt::Dof_Half)
        {
            texture = RENDER_TARGET(RendererRt::Dof_Half).get();
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == RendererRt::Dof_Half_2)
        {
            texture = RENDER_TARGET(RendererRt::Dof_Half_2).get();
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == RendererRt::Light_Volumetric)
        {
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == RendererRt::Brdf_Specular_Lut)
        {
            shader_type = RendererShader::Copy_Point_C;
        }

        // Acquire shaders
        RHI_Shader* shader = m_shaders[shader_type].get();
        if (!shader->IsCompiled())
            return false;

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute   = shader;
        pso.pass_name        = "Pass_DebugBuffer";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution    = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            m_buffer_uber_cpu.transform     = m_buffer_frame_cpu.view_projection_ortho;
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, texture);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }

        return true;
    }

    void Renderer::Pass_BrdfSpecularLut(RHI_CommandList* cmd_list)
    {
        if (m_brdf_specular_lut_rendered)
            return;

        // Acquire shaders
        RHI_Shader* shader = m_shaders[RendererShader::BrdfSpecularLut_C].get();
        if (!shader->IsCompiled())
            return;

        // Acquire render target
        RHI_Texture* render_target = RENDER_TARGET(RendererRt::Brdf_Specular_Lut).get();

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute   = shader;
        pso.pass_name        = "Pass_BrdfSpecularLut";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(render_target->GetWidth()), static_cast<float>(render_target->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(render_target->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(render_target->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsUav::rg, render_target);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();

            m_brdf_specular_lut_rendered = true;
        }
    }

    void Renderer::Pass_Copy(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::Copy_Point_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        pso.pass_name      = "Pass_Copy";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z   = 1;
            const bool async                      = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex,  tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_CopyBilinear(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::Copy_Bilinear_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso;
        pso.shader_compute  = shader_c;
        pso.pass_name       = "Pass_CopyBilinear";

        // Draw
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_CopyToBackbuffer(RHI_CommandList* cmd_list)
    {
        // Acquire shaders
        RHI_Shader* shader_v = m_shaders[RendererShader::Quad_V].get();
        RHI_Shader* shader_p = m_shaders[RendererShader::Copy_Point_P].get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pso = {};
        pso.shader_vertex            = shader_v;
        pso.shader_pixel             = shader_p;
        pso.rasterizer_state         = m_rasterizer_cull_back_solid.get();
        pso.blend_state              = m_blend_disabled.get();
        pso.depth_stencil_state      = m_depth_stencil_off_off.get();
        pso.vertex_buffer_stride     = m_viewport_quad.GetVertexBuffer()->GetStride();
        pso.render_target_swapchain  = m_swap_chain.get();
        pso.clear_color[0]           = rhi_color_dont_care;
        pso.primitive_topology       = RHI_PrimitiveTopology_TriangleList;
        pso.viewport                 = m_viewport;
        pso.pass_name                = "Pass_CopyToBackbuffer";

        // Record commands
        if (cmd_list->BeginRenderPass(pso))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(m_swap_chain->GetWidth()), static_cast<float>(m_swap_chain->GetHeight()));
            UpdateUberBuffer(cmd_list);

            cmd_list->SetTexture(RendererBindingsSrv::tex, RENDER_TARGET(RendererRt::Frame_PostProcess).get());
            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->DrawIndexed(m_viewport_quad.GetIndexCount());
            cmd_list->EndRenderPass();
        }
    }
}
