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
#include "Renderer.h"
#include "Window.h"
#include "bend_sss_cpu.h"
#include "../Profiling/Profiler.h"
#include "../World/Entity.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Components/ReflectionProbe.h"
#include "../World/Components/Transform.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_AMD_FidelityFX.h"
#include "../RHI/RHI_StructuredBuffer.h"
#include "../Display/Display.h"
//==============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        mutex mutex_generate_mips;
        const float thread_group_count = 8.0f;
        #define thread_group_count_x(tex) static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex->GetWidth())  / thread_group_count))
        #define thread_group_count_y(tex) static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex->GetHeight()) / thread_group_count))
    }

    void Renderer::SetGlobalShaderResources(RHI_CommandList* cmd_list)
    {
        // constant buffers
        cmd_list->SetConstantBuffer(Renderer_BindingsCb::frame,    GetConstantBuffer(Renderer_ConstantBuffer::Frame));
        cmd_list->SetConstantBuffer(Renderer_BindingsCb::light,    GetConstantBuffer(Renderer_ConstantBuffer::Light));
        cmd_list->SetConstantBuffer(Renderer_BindingsCb::material, GetConstantBuffer(Renderer_ConstantBuffer::Material));

        // textures
        cmd_list->SetTexture(Renderer_BindingsSrv::noise_normal, GetStandardTexture(Renderer_StandardTexture::Noise_normal));
        cmd_list->SetTexture(Renderer_BindingsSrv::noise_blue,   GetStandardTexture(Renderer_StandardTexture::Noise_blue));
    }

    void Renderer::Pass_Frame(RHI_CommandList* cmd_list)
    {
        SP_PROFILE_FUNCTION();

        // acquire render targets
        RHI_Texture* rt1       = GetRenderTarget(Renderer_RenderTexture::frame_render).get();
        RHI_Texture* rt2       = GetRenderTarget(Renderer_RenderTexture::frame_render_2).get();
        RHI_Texture* rt_output = GetRenderTarget(Renderer_RenderTexture::frame_output).get();

        UpdateConstantBufferFrame(cmd_list, false);

        if (shared_ptr<Camera> camera = GetCamera())
        { 
            // if there are no entities, clear to the camera's color
            if (GetEntities()[Renderer_Entity::Geometry].empty() && GetEntities()[Renderer_Entity::GeometryTransparent].empty() && GetEntities()[Renderer_Entity::Light].empty())
            {
                GetCmdList()->ClearRenderTarget(rt_output, 0, 0, false, camera->GetClearColor());
            }
            else // render frame
            {
                // generate brdf specular lut
                if (!m_brdf_specular_lut_rendered)
                {
                    Pass_BrdfSpecularLut(cmd_list);
                    m_brdf_specular_lut_rendered = true;
                }

                // determine if a transparent pass is required
                const bool do_transparent_pass = !GetEntities()[Renderer_Entity::GeometryTransparent].empty();

                // shadow maps
                {
                    Pass_ShadowMaps(cmd_list, false);
                    if (do_transparent_pass)
                    {
                        Pass_ShadowMaps(cmd_list, true);
                    }
                }

                Pass_ReflectionProbes(cmd_list);

                // opaque
                {
                    Pass_Depth_Prepass(cmd_list);
                    Pass_GBuffer(cmd_list);
                    Pass_Ssgi(cmd_list);
                    Pass_Ssr(cmd_list, rt1);
                    Pass_Sss_Bend(cmd_list);
                    Pass_Light(cmd_list);                  // compute diffuse and specular buffers
                    Pass_Light_Composition(cmd_list, rt1); // compose diffuse, specular, ssgi, volumetric etc.
                    Pass_Light_ImageBased(cmd_list, rt1);  // apply IBL and SSR
                }

                // transparent
                if (do_transparent_pass)
                {
                    // blit the frame so that refraction can sample from it
                    cmd_list->Copy(rt1, rt2, true);

                    // generate frame mips so that the reflections can simulate roughness
                    Pass_Ffx_Spd(cmd_list, rt2);

                    // blur the smaller mips to reduce blockiness/flickering
                    for (uint32_t i = 1; i < rt2->GetMipCount(); i++)
                    {
                        const bool depth_aware = false;
                        const float radius     = 1.0f;
                        const float sigma      = 12.0f;
                        Pass_Blur_Gaussian(cmd_list, rt2, depth_aware, radius, sigma, i);
                    }

                    Pass_Depth_Prepass(cmd_list, do_transparent_pass);
                    Pass_GBuffer(cmd_list, do_transparent_pass);
                    Pass_Ssr(cmd_list, rt1, do_transparent_pass);
                    Pass_Light(cmd_list, do_transparent_pass);
                    Pass_Light_Composition(cmd_list, rt1, do_transparent_pass);
                    Pass_Light_ImageBased(cmd_list, rt1, do_transparent_pass);
                }

                Pass_PostProcess(cmd_list);
            }

            // editor related stuff - passes that render on top of each other
            Pass_DebugMeshes(cmd_list, rt_output);
            Pass_Icons(cmd_list, rt_output);         
        }
        else
        {
            GetCmdList()->ClearRenderTarget(rt_output, 0, 0, false, Color::standard_black);
        }

        Pass_Text(cmd_list, rt_output);

        // transition the render target to a readable state so it can be rendered
        // within the viewport or copied to the swap chain back buffer
        rt_output->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list);
    }

    void Renderer::Pass_ShadowMaps(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        // All entities are rendered from the lights point of view.
        // Opaque entities write their depth information to a depth buffer, using just a vertex shader.
        // Transparent objects read the opaque depth but don't write their own, instead, they write their color information using a pixel shader.

        // acquire shaders
        RHI_Shader* shader_v           = GetShader(Renderer_Shader::depth_light_v).get();
        RHI_Shader* shader_instanced_v = GetShader(Renderer_Shader::depth_light_instanced_v).get();
        RHI_Shader* shader_p           = is_transparent_pass ? GetShader(Renderer_Shader::depth_light_p).get() : GetShader(Renderer_Shader::depth_alpha_test_p).get();
        if (!shader_v->IsCompiled() || !shader_instanced_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        cmd_list->BeginTimeblock(is_transparent_pass ? "shadow_maps_color" : "shadow_maps_depth");

        uint32_t start_index = !is_transparent_pass ? 0 : 2;
        uint32_t end_index   = !is_transparent_pass ? 2 : 4;
        for (uint32_t i = start_index; i < end_index; i++)
        {
            // acquire entities
            auto& entities = m_renderables[static_cast<Renderer_Entity>(i)];
            if (entities.empty())
                continue;

            // go through all of the lights
            auto& lights = GetEntities()[Renderer_Entity::Light];
            for (uint32_t light_index = 0; light_index < lights.size(); light_index++)
            {
                shared_ptr<Light> light = lights[light_index]->GetComponent<Light>();

                // can happen when loading a new scene and the lights get deleted
                if (!light)
                    continue;

                // skip lights which don't cast shadows or have an intensity of zero
                if (!light->GetShadowsEnabled() || light->GetIntensityWatt(GetCamera().get()) == 0.0f)
                    continue;

                // skip lights that don't cast transparent shadows (if this is a transparent pass)
                if (is_transparent_pass && !light->GetShadowsTransparentEnabled())
                    continue;

                // define pipeline state
                static RHI_PipelineState pso;
                pso.instancing                      = i == 1 || i == 3;
                pso.shader_vertex                   = !pso.instancing ? shader_v : shader_instanced_v;
                pso.shader_pixel                    = shader_p;
                pso.blend_state                     = is_transparent_pass ? GetBlendState(Renderer_BlendState::Alpha).get() : GetBlendState(Renderer_BlendState::Disabled).get();
                pso.depth_stencil_state             = is_transparent_pass ? GetDepthStencilState(Renderer_DepthStencilState::Depth_read).get() : GetDepthStencilState(Renderer_DepthStencilState::Depth_read_write_stencil_read).get();
                pso.render_target_color_textures[0] = light->GetColorTexture(); // always bind so we can clear to white (in case there are no transparent objects)
                pso.render_target_depth_texture     = light->GetDepthTexture();
                pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;
                pso.name                            = "Pass_ShadowMaps";

                // go through all of the cascades/faces
                for (uint32_t array_index = 0; array_index < pso.render_target_depth_texture->GetArrayLength(); array_index++)
                {
                    // set render target texture array index
                    pso.render_target_color_texture_array_index         = array_index;
                    pso.render_target_depth_stencil_texture_array_index = array_index;

                    // set clear values
                    pso.clear_color[0] = Color::standard_white;
                    pso.clear_depth    = (!pso.instancing && !is_transparent_pass) ? 0.0f : rhi_depth_load; // reverse-z

                    // set appropriate rasterizer state
                    if (light->GetLightType() == LightType::Directional)
                    {
                        // "Pancaking" - https://www.gamedev.net/forums/topic/639036-shadow-mapping-and-high-up-objects/
                        // it's basically a way to capture the silhouettes of potential shadow casters behind the light's view point.
                        // of course we also have to make sure that the light doesn't cull them in the first place (this is done automatically by the light)
                        pso.rasterizer_state = GetRasterizerState(Renderer_RasterizerState::Light_directional).get();

                        // don't do alpha testing for far away cascades, as it's not noticeable and it's a performance hit
                        pso.shader_pixel = array_index > 1 ? nullptr : pso.shader_pixel;
                    }
                    else
                    {
                        pso.rasterizer_state = GetRasterizerState(Renderer_RasterizerState::Light_point_spot).get();
                    }

                    // start render pass
                    cmd_list->SetPipelineState(pso);
                    if (!cmd_list->IsRenderPassActive())
                    {
                        cmd_list->BeginRenderPass();
                    }

                    // set light
                    UpdateConstantBufferLight(cmd_list, light, array_index);

                    // go through all of the entities
                    for (shared_ptr<Entity> entity : entities)
                    {
                        // acquire renderable component
                        shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>();
                        if (!renderable)
                            continue;

                        // skip meshes that don't cast shadows
                        if (!renderable->GetCastShadows())
                            continue;

                        // skip objects outside of the view frustum
                        if (!light->IsInViewFrustum(renderable, array_index))
                            continue;

                        // mesh can be null when async loading
                        Mesh* mesh = renderable->GetMesh();
                        if (!mesh)
                            continue;

                        // set vertex, index and instance buffers
                        {
                            cmd_list->SetBufferVertex(mesh->GetVertexBuffer());
                            if (pso.instancing)
                            {
                                cmd_list->SetBufferVertex(renderable->GetInstanceBuffer(), 1);
                            }

                            cmd_list->SetBufferIndex(mesh->GetIndexBuffer());
                        }

                        // set material
                        if (Material* material = renderable->GetMaterial())
                        {
                            BindTexturesMaterial(cmd_list, material);
                            UpdateConstantBufferMaterial(cmd_list, material);

                            m_cb_pass_cpu.set_f3_value(
                                material->HasTexture(MaterialTexture::AlphaMask) ? 1.0f : 0.0f,
                                material->HasTexture(MaterialTexture::Color)     ? 1.0f : 0.0f,
                                material->GetProperty(MaterialProperty::ColorA)
                            );
                        }

                        // set pass constants
                        m_cb_pass_cpu.transform = entity->GetTransform()->GetMatrix();
                        PushPassConstants(cmd_list);

                        // draw
                        cmd_list->DrawIndexed(
                            renderable->GetIndexCount(),
                            renderable->GetIndexOffset(),
                            renderable->GetVertexOffset(),
                            pso.instancing ? renderable->GetInstanceCount() : 1
                        );
                    }

                    if (cmd_list->IsRenderPassActive())
                    {
                        cmd_list->EndRenderPass();
                    }
                }
            }
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_ReflectionProbes(RHI_CommandList* cmd_list)
    {
        // acquire shaders
        RHI_Shader* shader_v = GetShader(Renderer_Shader::reflection_probe_v).get();
        RHI_Shader* shader_p = GetShader(Renderer_Shader::reflection_probe_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // acquire reflections probes
        const vector<shared_ptr<Entity>>& probes = m_renderables[Renderer_Entity::ReflectionProbe];
        if (probes.empty())
            return;

        // acquire renderables
        const vector<shared_ptr<Entity>>& renderables = m_renderables[Renderer_Entity::Geometry];
        if (renderables.empty())
            return;

        // acquire lights
        const vector<shared_ptr<Entity>>& lights = m_renderables[Renderer_Entity::Light];
        if (lights.empty())
            return;

        cmd_list->BeginTimeblock("reflection_probes");

        // for each reflection probe
        for (uint32_t probe_index = 0; probe_index < static_cast<uint32_t>(probes.size()); probe_index++)
        {
            shared_ptr<ReflectionProbe> probe = probes[probe_index]->GetComponent<ReflectionProbe>();
            if (!probe || !probe->GetNeedsToUpdate())
                continue;

            // define pipeline state
            static RHI_PipelineState pso;
            pso.shader_vertex                   = shader_v;
            pso.shader_pixel                    = shader_p;
            pso.rasterizer_state                = GetRasterizerState(Renderer_RasterizerState::Solid_cull_back).get();
            pso.blend_state                     = GetBlendState(Renderer_BlendState::Additive).get();
            pso.depth_stencil_state             = GetDepthStencilState(Renderer_DepthStencilState::Depth_read_write_stencil_read).get();
            pso.render_target_color_textures[0] = probe->GetColorTexture();
            pso.render_target_depth_texture     = probe->GetDepthTexture();
            pso.clear_color[0]                  = Color::standard_black;
            pso.clear_depth                     = 0.0f; // reverse-z
            pso.clear_stencil                   = rhi_stencil_dont_care;
            pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;

            // update cube faces
            uint32_t index_start = probe->GetUpdateFaceStartIndex();
            uint32_t index_end   = (index_start + probe->GetUpdateFaceCount()) % 7;
            for (uint32_t face_index = index_start; face_index < index_end; face_index++)
            {
                // set render target texture array index
                pso.render_target_color_texture_array_index = face_index;

                // set pipeline state
                cmd_list->SetPipelineState(pso);

                // begin render pass
                cmd_list->BeginRenderPass();

                // compute view projection matrix
                Matrix view_projection = probe->GetViewMatrix(face_index) * probe->GetProjectionMatrix();

                // for each renderable entity
                for (uint32_t index_renderable = 0; index_renderable < static_cast<uint32_t>(renderables.size()); index_renderable++)
                {
                    shared_ptr<Entity> entity = renderables[index_renderable];

                    // for each light entity
                    for (uint32_t index_light = 0; index_light < static_cast<uint32_t>(lights.size()); index_light++)
                    {
                        if (shared_ptr<Light> light = lights[index_light]->GetComponent<Light>())
                        {
                            if (light->GetIntensityWatt(GetCamera().get()) != 0)
                            {
                                // get renderable
                                shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>();
                                if (!renderable)
                                    continue;

                                // get material
                                Material* material = renderable->GetMaterial();
                                if (!material)
                                    continue;

                                // get geometry
                                Mesh* mesh = renderable->GetMesh();
                                if (!mesh || !mesh->GetVertexBuffer() || !mesh->GetIndexBuffer())
                                    continue;

                                // skip objects outside of the view frustum
                                if (!probe->IsInViewFrustum(renderable, face_index))
                                    continue;

                                // set geometry (will only happen if not already set)
                                cmd_list->SetBufferIndex(mesh->GetIndexBuffer());
                                cmd_list->SetBufferVertex(mesh->GetVertexBuffer());

                                // bind material textures
                                BindTexturesMaterial(cmd_list, material);

                                // set pass constants with cascade transform
                                m_cb_pass_cpu.transform = entity->GetTransform()->GetMatrix() * view_projection;
                                PushPassConstants(cmd_list);

                                // update light buffer
                                UpdateConstantBufferLight(cmd_list, light);

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

    void Renderer::Pass_Depth_Prepass(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        if (!GetOption<bool>(Renderer_Option::DepthPrepass))
            return;

        // acquire shaders
        RHI_Shader* shader_v           = GetShader(Renderer_Shader::depth_prepass_v).get();
        RHI_Shader* shader_instanced_v = GetShader(Renderer_Shader::depth_prepass_instanced_v).get();
        RHI_Shader* shader_p           = GetShader(Renderer_Shader::depth_alpha_test_p).get();
        if (!shader_v->IsCompiled() || !shader_instanced_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        cmd_list->BeginTimeblock(!is_transparent_pass ? "depth_prepass" : "depth_prepass_transparent");

        uint32_t start_index = !is_transparent_pass ? 0 : 2;
        uint32_t end_index   = !is_transparent_pass ? 2 : 4;
        bool is_first_pass   = true;
        for (uint32_t i = start_index; i < end_index; i++)
        {
            // acquire entities
            auto& entities = m_renderables[static_cast<Renderer_Entity>(i)];
            if (entities.empty())
                continue;

            // define pipeline state
            static RHI_PipelineState pso;
            pso.name                        = !is_transparent_pass ? "depth_prepass" : "depth_prepass_transparent";
            pso.instancing                  = i == 1;
            pso.shader_vertex               = !pso.instancing ? shader_v : shader_instanced_v;
            pso.shader_pixel                = shader_p; // alpha testing
            pso.rasterizer_state            = GetRasterizerState(Renderer_RasterizerState::Solid_cull_back).get();
            pso.blend_state                 = GetBlendState(Renderer_BlendState::Disabled).get();
            pso.depth_stencil_state         = GetDepthStencilState(Renderer_DepthStencilState::Depth_read_write_stencil_read).get();
            pso.render_target_depth_texture = GetRenderTarget(Renderer_RenderTexture::gbuffer_depth).get();
            pso.clear_depth                 = (is_transparent_pass || pso.instancing) ? rhi_depth_load : 0.0f; // reverse-z
            pso.primitive_topology          = RHI_PrimitiveTopology_Mode::TriangleList;

            // begin render pass
            cmd_list->SetPipelineState(pso);
            cmd_list->BeginRenderPass();

            uint64_t bound_material_id = 0;
            for (shared_ptr<Entity> entity : entities)
            {
                // get renderable
                shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>();
                if (!renderable)
                    continue;

                // get material
                Material* material = renderable->GetMaterial();
                if (!material)
                    continue;

                // skip objects outside of the view frustum
                if (!GetCamera()->IsInViewFrustum(renderable))
                    continue;

                // mesh can be null when async loading
                Mesh* mesh = renderable->GetMesh();
                if (!mesh)
                    continue;

                // set vertex, index and instance buffers
                {
                    cmd_list->SetBufferVertex(mesh->GetVertexBuffer());
                    if (pso.instancing)
                    {
                        cmd_list->SetBufferVertex(renderable->GetInstanceBuffer(), 1);
                    }

                    cmd_list->SetBufferIndex(mesh->GetIndexBuffer());
                }

                // set alpha testing textures
                if (bound_material_id != material->GetObjectId())
                {
                    BindTexturesMaterial(cmd_list, material);
                    bound_material_id = material->GetObjectId();
                }

                // set pass constants
                {
                    m_cb_pass_cpu.transform = entity->GetTransform()->GetMatrix();
                    m_cb_pass_cpu.set_f3_value(
                        material->HasTexture(MaterialTexture::AlphaMask) ? 1.0f : 0.0f,
                        material->HasTexture(MaterialTexture::Color) ? 1.0f : 0.0f,
                        material->GetProperty(MaterialProperty::ColorA)
                    );

                    PushPassConstants(cmd_list);
                }

                // draw
                cmd_list->DrawIndexed(
                    renderable->GetIndexCount(),
                    renderable->GetIndexOffset(),
                    renderable->GetVertexOffset(),
                    pso.instancing ? renderable->GetInstanceCount() : 1
                );
            }

            cmd_list->EndRenderPass();
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_GBuffer(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        // acquire shaders
        RHI_Shader* shader_v           = GetShader(Renderer_Shader::gbuffer_v).get();
        RHI_Shader* shader_v_instanced = GetShader(Renderer_Shader::gbuffer_instanced_v).get();
        RHI_Shader* shader_p           = GetShader(Renderer_Shader::gbuffer_p).get();
        if (!shader_v->IsCompiled() || !shader_v_instanced->IsCompiled() || !shader_p->IsCompiled())
            return;

        cmd_list->BeginTimeblock(is_transparent_pass ? "g_buffer_transparent" : "g_buffer");

        // acquire render targets
        RHI_Texture* tex_albedo            = GetRenderTarget(Renderer_RenderTexture::gbuffer_albedo).get();
        RHI_Texture* tex_normal            = GetRenderTarget(Renderer_RenderTexture::gbuffer_normal).get();
        RHI_Texture* tex_material          = GetRenderTarget(Renderer_RenderTexture::gbuffer_material).get();
        RHI_Texture* tex_material_2        = GetRenderTarget(Renderer_RenderTexture::gbuffer_material_2).get();
        RHI_Texture* tex_velocity          = GetRenderTarget((GetFrameNum() % 2 == 0) ? Renderer_RenderTexture::gbuffer_velocity : Renderer_RenderTexture::gbuffer_velocity_previous).get();
        RHI_Texture* tex_depth             = GetRenderTarget(Renderer_RenderTexture::gbuffer_depth).get();
        RHI_Texture* tex_fsr2_transparency = GetRenderTarget(Renderer_RenderTexture::fsr2_mask_transparency).get();

        bool depth_prepass = GetOption<bool>(Renderer_Option::DepthPrepass);
        bool wireframe     = GetOption<bool>(Renderer_Option::Debug_Wireframe);

        // deduce rasterizer state
        RHI_RasterizerState* rasterizer_state = is_transparent_pass ? GetRasterizerState(Renderer_RasterizerState::Solid_cull_none).get() : GetRasterizerState(Renderer_RasterizerState::Solid_cull_back).get();
        rasterizer_state                      = wireframe ? GetRasterizerState(Renderer_RasterizerState::Wireframe_cull_none).get() : rasterizer_state;

        // deduce depth-stencil state
        RHI_DepthStencilState* depth_stencil_state = depth_prepass ? GetDepthStencilState(Renderer_DepthStencilState::Depth_read).get() : GetDepthStencilState(Renderer_DepthStencilState::Depth_read_write_stencil_read).get();

        // note: if is_transparent_pass is true we could simply clear the RTs, however we don't do this as fsr
        // can be enabled, and if it is, it will expect the RTs to contain both the opaque and transparent data
      
        uint32_t start_index = !is_transparent_pass ? 0 : 2;
        uint32_t end_index   = !is_transparent_pass ? 2 : 4;
        bool is_first_pass   = true;
        for (uint32_t i = start_index; i < end_index; i++)
        {
            // acquire entities
            auto& entities = m_renderables[static_cast<Renderer_Entity>(i)];
            if (entities.empty())
                continue;

            // define pipeline state
            RHI_PipelineState pso;
            pso.name                            = is_transparent_pass ? "g_buffer_transparent" : "g_buffer";
            pso.instancing                      = i == 1 || i == 3;
            pso.shader_pixel                    = shader_p;
            pso.shader_vertex                   = pso.instancing ? shader_v_instanced : shader_v;
            pso.blend_state                     = GetBlendState(Renderer_BlendState::Disabled).get();
            pso.rasterizer_state                = rasterizer_state;
            pso.depth_stencil_state             = depth_stencil_state;
            pso.render_target_color_textures[0] = tex_albedo;
            pso.clear_color[0]                  = (!is_first_pass || pso.instancing || is_transparent_pass) ? rhi_color_load : Color::standard_transparent;
            pso.render_target_color_textures[1] = tex_normal;
            pso.clear_color[1]                  = pso.clear_color[0];
            pso.render_target_color_textures[2] = tex_material;
            pso.clear_color[2]                  = pso.clear_color[0];
            pso.render_target_color_textures[3] = tex_material_2;
            pso.clear_color[3]                  = pso.clear_color[0];
            pso.render_target_color_textures[4] = tex_velocity;
            pso.clear_color[4]                  = pso.clear_color[0];
            pso.render_target_color_textures[5] = tex_fsr2_transparency;
            pso.clear_color[5]                  = pso.clear_color[0];
            pso.render_target_depth_texture     = tex_depth;
            pso.clear_depth                     = (!is_first_pass || depth_prepass) ? rhi_depth_load : 0.0f; // reverse-z
            pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;

            // begin render pass
            cmd_list->SetPipelineState(pso);
            cmd_list->BeginRenderPass();

            uint64_t bound_material_id = 0;
            for (shared_ptr<Entity> entity : entities)
            {
                // get renderable
                shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>();
                if (!renderable)
                    continue;

                // skip objects outside of the view frustum
                if (!GetCamera()->IsInViewFrustum(renderable))
                    continue;

                // mesh can be null when async loading
                Mesh* mesh = renderable->GetMesh();
                if (!mesh)
                    continue;

                // set vertex, index and instance buffers
                {
                    cmd_list->SetBufferVertex(mesh->GetVertexBuffer());
                    if (pso.instancing)
                    {
                        cmd_list->SetBufferVertex(renderable->GetInstanceBuffer(), 1);
                    }

                    cmd_list->SetBufferIndex(mesh->GetIndexBuffer());
                }

                // set material
                if (Material* material = renderable->GetMaterial())
                {
                    if (bound_material_id != material->GetObjectId())
                    {
                        BindTexturesMaterial(cmd_list, material);
                        UpdateConstantBufferMaterial(cmd_list, material);
                        bound_material_id = material->GetObjectId();
                    }
                }

                // push pass constants
                {
                    m_cb_pass_cpu.set_is_transparent(is_transparent_pass);

                    // update transform
                    m_cb_pass_cpu.transform = entity->GetTransform()->GetMatrix();
                    m_cb_pass_cpu.set_transform_previous(entity->GetTransform()->GetMatrixPrevious());
                    entity->GetTransform()->SetMatrixPrevious(m_cb_pass_cpu.transform);

                    PushPassConstants(cmd_list);
                }

                // draw
                cmd_list->DrawIndexed(
                    renderable->GetIndexCount(),
                    renderable->GetIndexOffset(),
                    renderable->GetVertexOffset(),
                    pso.instancing ? renderable->GetInstanceCount() : 1
                );

                is_first_pass = false;
                Profiler::m_renderer_meshes_rendered++;
            }

            cmd_list->EndRenderPass();
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Ssgi(RHI_CommandList* cmd_list)
    {
        if (!GetOption<bool>(Renderer_Option::ScreenSpaceGlobalIllumination))
            return;

        // acquire shaders
        RHI_Shader* shader_ssgi   = GetShader(Renderer_Shader::ssgi_c).get();
        RHI_Shader* shader_filter = GetShader(Renderer_Shader::temporal_filter_c).get();
        if (!shader_ssgi->IsCompiled() || !shader_filter->IsCompiled())
            return;

        // acquire render targets
        RHI_Texture* tex_ssgi          = GetRenderTarget(Renderer_RenderTexture::ssgi).get();
        RHI_Texture* tex_ssgi_filtered = GetRenderTarget(Renderer_RenderTexture::ssgi_filtered).get();

        cmd_list->BeginTimeblock("ssgi");

        // ssgi
        {
            // define pipeline state
            static RHI_PipelineState pso;
            pso.name           = "ssgi";
            pso.shader_compute = shader_ssgi;

            // set pipeline state
            cmd_list->SetPipelineState(pso);

            // set pass constants
            m_cb_pass_cpu.set_resolution_out(tex_ssgi);
            PushPassConstants(cmd_list);

            // set textures
            BindTexturesGfbuffer(cmd_list);
            cmd_list->SetTexture(Renderer_BindingsUav::tex,           tex_ssgi);
            cmd_list->SetTexture(Renderer_BindingsSrv::light_diffuse, GetRenderTarget(Renderer_RenderTexture::light_diffuse));

            // render
            cmd_list->Dispatch(thread_group_count_x(tex_ssgi), thread_group_count_y(tex_ssgi));
            cmd_list->InsertMemoryBarrierImageWaitForWrite(tex_ssgi);
        }

        cmd_list->BeginMarker("ssgi_temporal_filterintg");
        {
            // define pipeline state
            static RHI_PipelineState pso;
            pso.name           = "ssgi_temporal_filterintg";
            pso.shader_compute = shader_filter;

            // set pipeline state
            cmd_list->SetPipelineState(pso);

            // set pass constants
            m_cb_pass_cpu.set_resolution_out(tex_ssgi);
            PushPassConstants(cmd_list);

            // set textures
            BindTexturesGfbuffer(cmd_list);
            cmd_list->SetTexture(Renderer_BindingsUav::tex,  tex_ssgi_filtered);
            cmd_list->SetTexture(Renderer_BindingsUav::tex2, tex_ssgi);
                                                             
            // render
            cmd_list->Dispatch(thread_group_count_x(tex_ssgi), thread_group_count_y(tex_ssgi));
        }
        cmd_list->EndMarker();

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Ssr(RHI_CommandList* cmd_list, RHI_Texture* tex_in, const bool is_transparent_pass)
    {
        if (!GetOption<bool>(Renderer_Option::ScreenSpaceReflections))
            return;

        // acquire shaders
        RHI_Shader* shader_c = GetShader(Renderer_Shader::ssr_c).get();
        if (!shader_c->IsCompiled())
            return;

        // acquire render targets
        RHI_Texture* tex_ssr = GetRenderTarget(Renderer_RenderTexture::ssr).get();

        cmd_list->BeginTimeblock(!is_transparent_pass ? "ssr" : "ssr_transparent");

        // define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // set pipeline state
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_cb_pass_cpu.set_resolution_out(tex_ssr);
        m_cb_pass_cpu.set_is_transparent(is_transparent_pass);
        PushPassConstants(cmd_list);

        // set textures
        BindTexturesGfbuffer(cmd_list);
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_ssr); // write to that
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);  // reflect from that

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_ssr), thread_group_count_y(tex_ssr));

        // generate frame mips so that we can simulate roughness
        Pass_Ffx_Spd(cmd_list, tex_ssr);

        // blur the smaller mips to reduce blockiness/flickering
        for (uint32_t i = 1; i < tex_ssr->GetMipCount(); i++)
        {
            const bool depth_aware = true;
            const float radius     = 5.0f;
            const float sigma      = 2.0f;
            Pass_Blur_Gaussian(cmd_list, tex_ssr, depth_aware, radius, sigma, i);
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Sss_Bend(RHI_CommandList* cmd_list)
    {
        if (!GetOption<bool>(Renderer_Option::ScreenSpaceShadows))
            return;

        // acquire shaders
        RHI_Shader* shader_c = GetShader(Renderer_Shader::sss_c_bend).get();
        if (!shader_c->IsCompiled())
            return;

        // acquire lights
        const vector<shared_ptr<Entity>>& entities = m_renderables[Renderer_Entity::Light];
        if (entities.empty())
            return;

        // acquire render targets
        RHI_Texture* tex_sss = GetRenderTarget(Renderer_RenderTexture::sss).get();

        cmd_list->BeginTimeblock("sss_bend");
        {
            // define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_c;

            // set pipeline state
            cmd_list->SetPipelineState(pso);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, GetRenderTarget(Renderer_RenderTexture::gbuffer_depth));  // read from that
            cmd_list->SetTexture(Renderer_BindingsUav::tex_array2, tex_sss); // write to that

            // iterate through all the lights
            static float array_slice_index = 0.0f;
            for (shared_ptr<Entity> entity : entities)
            {
                if (shared_ptr<Light> light = entity->GetComponent<Light>())
                {
                    if (!light->GetShadowsEnabled())
                        continue;

                    if (array_slice_index == tex_sss->GetArrayLength())
                    {
                        SP_LOG_WARNING("Render target has reached the maximum number of lights it can hold");
                        break;
                    }

                    float near = 1.0f;
                    float far  = 0.0f;
                    Math::Matrix view_projection = m_camera->GetViewProjectionMatrix();
                    Vector4 p = {};
                    if (light->GetLightType() == LightType::Directional)
                    {
                        // TODO: Why do we need to flip sign?
                        p = Vector4(-light->GetTransform()->GetForward().Normalized(), 0.0f) * view_projection;
                    }
                    else
                    {
                        p = Vector4(light->GetTransform()->GetPosition(), 1.0f) * view_projection;
                    }

                    float in_light_projection[]      = { p.x, p.y, p.z, p.w };
                    int32_t in_viewport_size[]       = { static_cast<int32_t>(tex_sss->GetWidth()), static_cast<int32_t>(tex_sss->GetHeight()) };
                    int32_t in_min_render_bounds[]   = { 0, 0 };
                    int32_t in_max_render_bounds[]   = { static_cast<int32_t>(tex_sss->GetWidth()), static_cast<int32_t>(tex_sss->GetHeight()) };
                    Bend::DispatchList dispatch_list = Bend::BuildDispatchList(in_light_projection, in_viewport_size, in_min_render_bounds, in_max_render_bounds, false);

                    m_cb_pass_cpu.set_f4_value
                    (
                        dispatch_list.LightCoordinate_Shader[0],
                        dispatch_list.LightCoordinate_Shader[1],
                        dispatch_list.LightCoordinate_Shader[2],
                        dispatch_list.LightCoordinate_Shader[3]
                    );

                    // light index writes into the texture array index
                    m_cb_pass_cpu.set_f3_value(near, far, array_slice_index++);
                    m_cb_pass_cpu.set_f3_value2(1.0f / tex_sss->GetWidth(), 1.0f / tex_sss->GetHeight(), 0.0f);

                    for (int32_t dispatch_index = 0; dispatch_index < dispatch_list.DispatchCount; ++dispatch_index)
                    {
                        const Bend::DispatchData& dispatch = dispatch_list.Dispatch[dispatch_index];
                        m_cb_pass_cpu.set_resolution_in({ dispatch.WaveOffset_Shader[0], dispatch.WaveOffset_Shader[1] });
                        PushPassConstants(cmd_list);
                        cmd_list->Dispatch(dispatch.WaveCount[0], dispatch.WaveCount[1], dispatch.WaveCount[2]);
                    }
                }
            }

            array_slice_index = 0;
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Light(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        // acquire shaders
        RHI_Shader* shader_c = GetShader(Renderer_Shader::light_c).get();
        if (!shader_c->IsCompiled())
            return;

        // acquire lights
        const vector<shared_ptr<Entity>>& entities = m_renderables[Renderer_Entity::Light];
        if (entities.empty())
            return;

        cmd_list->BeginTimeblock(is_transparent_pass ? "light_transparent" : "light");

        // acquire render targets
        RHI_Texture* tex_diffuse    = is_transparent_pass ? GetRenderTarget(Renderer_RenderTexture::light_diffuse_transparent).get()  : GetRenderTarget(Renderer_RenderTexture::light_diffuse).get();
        RHI_Texture* tex_specular   = is_transparent_pass ? GetRenderTarget(Renderer_RenderTexture::light_specular_transparent).get() : GetRenderTarget(Renderer_RenderTexture::light_specular).get();
        RHI_Texture* tex_volumetric = GetRenderTarget(Renderer_RenderTexture::light_volumetric).get();

        // clear render targets
        cmd_list->ClearRenderTarget(tex_diffuse,    0, 0, true, Color::standard_black);
        cmd_list->ClearRenderTarget(tex_specular,   0, 0, true, Color::standard_black);
        cmd_list->ClearRenderTarget(tex_volumetric, 0, 0, true, Color::standard_black);

        // define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // set pipeline state
        cmd_list->SetPipelineState(pso);

        // iterate through all the lights
        static float array_slice_index = 0.0f;
        for (shared_ptr<Entity> entity : entities)
        {
            if (shared_ptr<Light> light = entity->GetComponent<Light>())
            {
                // do the lighting even when intensity is zero, since we can have emissive lighting.
                BindTexturesGfbuffer(cmd_list);
                cmd_list->SetTexture(Renderer_BindingsUav::tex,  tex_diffuse);
                cmd_list->SetTexture(Renderer_BindingsUav::tex2, tex_specular);
                cmd_list->SetTexture(Renderer_BindingsUav::tex3, tex_volumetric);
                cmd_list->SetTexture(Renderer_BindingsSrv::ssgi, GetRenderTarget(Renderer_RenderTexture::ssgi_filtered));
                
                // set shadow maps
                if (light->GetShadowsEnabled())
                {
                    RHI_Texture* tex_color = light->GetShadowsTransparentEnabled() ? light->GetColorTexture() : nullptr;

                    if (light->GetLightType() == LightType::Directional)
                    {
                        cmd_list->SetTexture(Renderer_BindingsSrv::light_directional_depth, light->GetDepthTexture());
                        cmd_list->SetTexture(Renderer_BindingsSrv::light_directional_color, tex_color);
                    }
                    else if (light->GetLightType() == LightType::Point)
                    {
                        cmd_list->SetTexture(Renderer_BindingsSrv::light_point_depth, light->GetDepthTexture());
                        cmd_list->SetTexture(Renderer_BindingsSrv::light_point_color, tex_color);
                    }
                    else if (light->GetLightType() == LightType::Spot)
                    {
                        cmd_list->SetTexture(Renderer_BindingsSrv::light_spot_depth, light->GetDepthTexture());
                        cmd_list->SetTexture(Renderer_BindingsSrv::light_spot_color, tex_color);
                    }

                    // light index reads from the texture array index (sss)
                    if (light->GetShadowsEnabled())
                    {
                        m_cb_pass_cpu.set_f3_value2(array_slice_index++, 0.0f, 0.0f);
                    }
                    cmd_list->SetTexture(Renderer_BindingsSrv::sss, GetRenderTarget(Renderer_RenderTexture::sss));
                }
                
                // update light buffer
                UpdateConstantBufferLight(cmd_list, light);
                
                // push pass constants
                m_cb_pass_cpu.set_resolution_out(tex_diffuse);
                m_cb_pass_cpu.set_is_transparent(is_transparent_pass);
                m_cb_pass_cpu.set_f3_value(0.0f, GetOption<float>(Renderer_Option::ShadowResolution), 0.0f);
                PushPassConstants(cmd_list);
                
                cmd_list->Dispatch(thread_group_count_x(tex_diffuse), thread_group_count_y(tex_diffuse));
            }

            array_slice_index = 0;
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Light_Composition(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass)
    {
        // acquire shaders
        RHI_Shader* shader_c = GetShader(Renderer_Shader::light_composition_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock(is_transparent_pass ? "light_composition_transparent" : "light_composition");

        // define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // set pipeline state
        cmd_list->SetPipelineState(pso);

        // push pass constants
        m_cb_pass_cpu.set_resolution_out(tex_out);
        m_cb_pass_cpu.set_is_transparent(is_transparent_pass);
        m_cb_pass_cpu.set_f3_value(static_cast<float>(GetRenderTarget(Renderer_RenderTexture::frame_render)->GetMipCount()), 0.0f, 0.0f);
        PushPassConstants(cmd_list);

        // update light buffer with the directional light
        {
            const vector<shared_ptr<Entity>>& entities = m_renderables[Renderer_Entity::Light];
            for (shared_ptr<Entity> entity : entities)
            {
                if (entity->GetComponent<Light>()->GetLightType() == LightType::Directional)
                {
                    UpdateConstantBufferLight(cmd_list, entity->GetComponent<Light>());
                    break;
                }
            }
        }

        // set textures
        BindTexturesGfbuffer(cmd_list);
        cmd_list->SetTexture(Renderer_BindingsUav::tex,              tex_out);
        cmd_list->SetTexture(Renderer_BindingsSrv::light_diffuse,    is_transparent_pass ? GetRenderTarget(Renderer_RenderTexture::light_diffuse_transparent).get()  : GetRenderTarget(Renderer_RenderTexture::light_diffuse).get());
        cmd_list->SetTexture(Renderer_BindingsSrv::light_specular,   is_transparent_pass ? GetRenderTarget(Renderer_RenderTexture::light_specular_transparent).get() : GetRenderTarget(Renderer_RenderTexture::light_specular).get());
        cmd_list->SetTexture(Renderer_BindingsSrv::light_volumetric, GetRenderTarget(Renderer_RenderTexture::light_volumetric));
        cmd_list->SetTexture(Renderer_BindingsSrv::frame,            GetRenderTarget(Renderer_RenderTexture::frame_render_2)); // refraction
        cmd_list->SetTexture(Renderer_BindingsSrv::ssgi,             GetRenderTarget(Renderer_RenderTexture::ssgi_filtered));
        cmd_list->SetTexture(Renderer_BindingsSrv::environment,      GetEnvironmentTexture());

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Light_ImageBased(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass)
    {
        // acquire shaders
        RHI_Shader* shader_v = GetShader(Renderer_Shader::fullscreen_triangle_v).get();
        RHI_Shader* shader_p = GetShader(Renderer_Shader::light_image_based_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        cmd_list->BeginTimeblock(is_transparent_pass ? "light_image_based_transparent" : "light_image_based");

        // get reflection probe entities
        const vector<shared_ptr<Entity>>& probes = m_renderables[Renderer_Entity::ReflectionProbe];

        // define pipeline state
        static RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v;
        pso.shader_pixel                    = shader_p;
        pso.rasterizer_state                = GetRasterizerState(Renderer_RasterizerState::Solid_cull_back).get();
        pso.depth_stencil_state             = GetDepthStencilState(Renderer_DepthStencilState::Off).get();
        pso.blend_state                     = GetBlendState(Renderer_BlendState::Additive).get();
        pso.render_target_color_textures[0] = tex_out;
        pso.clear_color[0]                  = rhi_color_load;
        pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;
        pso.is_fullscreen_triangle          = true;

        // set pipeline state
        cmd_list->SetPipelineState(pso);

        // set textures
        BindTexturesGfbuffer(cmd_list);
        cmd_list->SetTexture(Renderer_BindingsSrv::ssgi,        GetRenderTarget(Renderer_RenderTexture::ssgi_filtered));
        cmd_list->SetTexture(Renderer_BindingsSrv::ssr,         GetRenderTarget(Renderer_RenderTexture::ssr));
        cmd_list->SetTexture(Renderer_BindingsSrv::sss,         GetRenderTarget(Renderer_RenderTexture::sss));
        cmd_list->SetTexture(Renderer_BindingsSrv::lutIbl,      GetRenderTarget(Renderer_RenderTexture::brdf_specular_lut));
        cmd_list->SetTexture(Renderer_BindingsSrv::environment, GetEnvironmentTexture());

        // set probe textures and data
        if (!probes.empty())
        {
            shared_ptr<ReflectionProbe> probe = probes[0]->GetComponent<ReflectionProbe>();

            cmd_list->SetTexture(Renderer_BindingsSrv::reflection_probe, probe->GetColorTexture());
            m_cb_pass_cpu.set_f3_value(probe->GetTransform()->GetPosition());
            m_cb_pass_cpu.set_f3_value2(probe->GetExtents());
        }

        // set pass constants
        m_cb_pass_cpu.set_resolution_out(tex_out);
        m_cb_pass_cpu.set_is_transparent(is_transparent_pass);
        m_cb_pass_cpu.set_f4_value(!probes.empty() ? 1.0f : 0.0f, static_cast<float>(GetRenderTarget(Renderer_RenderTexture::ssr)->GetMipCount()), GetOption<float>(Renderer_Option::Fog), 0.0f); // reflection probe available
        PushPassConstants(cmd_list);

        // update light buffer with the directional light
        {
            const vector<shared_ptr<Entity>>& entities = m_renderables[Renderer_Entity::Light];
            for (shared_ptr<Entity> entity : entities)
            {
                if (entity->GetComponent<Light>()->GetLightType() == LightType::Directional)
                {
                    UpdateConstantBufferLight(cmd_list, entity->GetComponent<Light>());
                    break;
                }
            }
        }

        // render
        cmd_list->BeginRenderPass();
        {
            cmd_list->Draw(3, 0);
            cmd_list->EndRenderPass();
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Blur_Gaussian(RHI_CommandList* cmd_list, RHI_Texture* tex_in, const bool depth_aware, const float radius, const float sigma, const uint32_t mip /*= all_mips*/)
    {
        // acquire shaders
        RHI_Shader* shader_c = GetShader(depth_aware ? Renderer_Shader::blur_gaussian_bilaterial_c : Renderer_Shader::blur_gaussian_c).get();
        if (!shader_c->IsCompiled())
            return;

        const float pixel_stride = 1.0f;
        const bool mip_requested = mip != rhi_all_mips;
        const uint32_t mip_range = mip_requested ? 1 : 0;

        // if we need to blur a specific mip, ensure that the texture has per mip views
        if (mip_requested)
        {
            SP_ASSERT(tex_in->HasPerMipViews());
        }

        // compute width and height
        const uint32_t width  = mip_requested ? (tex_in->GetWidth()  >> mip) : tex_in->GetWidth();
        const uint32_t height = mip_requested ? (tex_in->GetHeight() >> mip) : tex_in->GetHeight();

        // acquire render targets
        RHI_Texture* tex_depth  = GetRenderTarget(Renderer_RenderTexture::gbuffer_depth).get();
        RHI_Texture* tex_normal = GetRenderTarget(Renderer_RenderTexture::gbuffer_normal).get();
        RHI_Texture* tex_blur   = GetRenderTarget(Renderer_RenderTexture::blur).get();

        // ensure that the blur scratch texture is big enough
        SP_ASSERT(tex_blur->GetWidth() >= width && tex_blur->GetHeight() >= height);

        // compute thread group count
        const uint32_t thread_group_count_x_ = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(width) / thread_group_count));
        const uint32_t thread_group_count_y_ = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(height) / thread_group_count));

        cmd_list->BeginMarker("blur_gaussian");

        // horizontal pass
        {
            // define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_c;

            // set pipeline state
            cmd_list->SetPipelineState(pso);

            // set pass constants
            m_cb_pass_cpu.set_resolution_in(Vector2(static_cast<float>(width), static_cast<float>(height)));
            m_cb_pass_cpu.set_resolution_out(tex_blur);
            m_cb_pass_cpu.set_f3_value(pixel_stride, 0.0f, radius);
            m_cb_pass_cpu.set_f3_value2(sigma);
            PushPassConstants(cmd_list);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_blur);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in, mip, mip_range);
            if (depth_aware)
            {
                BindTexturesGfbuffer(cmd_list);
            }

            // render
            cmd_list->Dispatch(thread_group_count_x_, thread_group_count_y_);
        }

        // vertical pass
        {
            // define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_c;

            // set pipeline state
            cmd_list->SetPipelineState(pso);

            // set pass constants
            m_cb_pass_cpu.set_resolution_in(Vector2(static_cast<float>(width), static_cast<float>(height)));
            m_cb_pass_cpu.set_resolution_out(tex_blur);
            m_cb_pass_cpu.set_f3_value(0.0f, pixel_stride, radius);
            m_cb_pass_cpu.set_f3_value2(sigma);
            PushPassConstants(cmd_list);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_in, mip, mip_range);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_blur);
            if (depth_aware)
            {
                BindTexturesGfbuffer(cmd_list);
            }

            // render
            cmd_list->Dispatch(thread_group_count_x_, thread_group_count_y_);
        }

        cmd_list->EndMarker();
    }

    void Renderer::Pass_PostProcess(RHI_CommandList* cmd_list)
    {
        // IN:  Frame_Render, which is and HDR render resolution render target (with a second texture so passes can alternate between them)
        // OUT: Frame_Output, which is and LDR output resolution render target (with a second texture so passes can alternate between them)

        // Acquire render targets (as references so that swapping the pointers around works)
        RHI_Texture* rt_frame_render         = GetRenderTarget(Renderer_RenderTexture::frame_render).get();   // render res
        RHI_Texture* rt_frame_render_scratch = GetRenderTarget(Renderer_RenderTexture::frame_render_2).get(); // render res
        RHI_Texture* rt_frame_output         = GetRenderTarget(Renderer_RenderTexture::frame_output).get();   // output res
        RHI_Texture* rt_frame_output_scratch = GetRenderTarget(Renderer_RenderTexture::frame_output_2).get(); // output res

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
            if (GetOption<bool>(Renderer_Option::DepthOfField))
            {
                swap_render = !swap_render;
                Pass_DepthOfField(cmd_list, get_render_in, get_render_out);
            }

            // Debug rendering (world grid, vectors, debugging etc)
            {
                // clear the reactive mask since all the debug primitives have no motion vectors and need to be added to the mask
                RHI_Texture* tex_reactive_mask = GetRenderTarget(Renderer_RenderTexture::fsr2_mask_reactive).get();
                cmd_list->ClearRenderTarget(tex_reactive_mask, 0, 0, false, Color::standard_black);

                Pass_Grid(cmd_list, get_render_out);
                Pass_Lines(cmd_list, get_render_out);
                Pass_Outline(cmd_list, get_render_out);
            }
        }

        // Determine antialiasing modes
        Renderer_Antialiasing antialiasing = GetOption<Renderer_Antialiasing>(Renderer_Option::Antialiasing);
        bool taa_enabled                   = antialiasing == Renderer_Antialiasing::Taa  || antialiasing == Renderer_Antialiasing::TaaFxaa;
        bool fxaa_enabled                  = antialiasing == Renderer_Antialiasing::Fxaa || antialiasing == Renderer_Antialiasing::TaaFxaa;

        // Get upsampling mode
        Renderer_Upsampling upsampling_mode = GetOption<Renderer_Upsampling>(Renderer_Option::Upsampling);

        // RENDER RESOLUTION -> OUTPUT RESOLUTION
        {
            // FSR 2.0 (It can be used both for upsampling and just TAA)
            if (upsampling_mode == Renderer_Upsampling::FSR2 || taa_enabled)
            {
                swap_render = !swap_render;
                Pass_Ffx_Fsr2(cmd_list, get_render_in, rt_frame_output);
            }
            // Linear
            else if (upsampling_mode == Renderer_Upsampling::Linear)
            {
                swap_render = !swap_render;
                cmd_list->Blit(get_render_in, rt_frame_output, false);
            }
        }

        // OUTPUT RESOLUTION
        {
            // motion Blur
            if (GetOption<bool>(Renderer_Option::MotionBlur))
            {
                swap_output = !swap_output;
                Pass_MotionBlur(cmd_list, get_output_in, get_output_out);
            }

            // bloom
            if (GetOption<bool>(Renderer_Option::Bloom))
            {
                swap_output = !swap_output;
                Pass_Bloom(cmd_list, get_output_in, get_output_out);
            }

            // tone-mapping & gamma correction
            swap_output = !swap_output;
            Pass_ToneMappingGammaCorrection(cmd_list, get_output_in, get_output_out);

            // sharpening
            if (GetOption<bool>(Renderer_Option::Sharpness))
            {
                swap_output = !swap_output;
                Pass_Ffx_Cas(cmd_list, get_output_in, get_output_out);
            }

            // debanding
            if (GetOption<bool>(Renderer_Option::Debanding))
            {
                swap_output = !swap_output;
                Pass_Debanding(cmd_list, get_output_in, get_output_out);
            }

            // fxaa
            if (fxaa_enabled)
            {
                swap_output = !swap_output;
                Pass_Fxaa(cmd_list, get_output_in, get_output_out);
            }

            // chromatic aberration
            if (GetOption<bool>(Renderer_Option::ChromaticAberration))
            {
                swap_output = !swap_output;
                Pass_ChromaticAberration(cmd_list, get_output_in, get_output_out);
            }

            // film grain
            if (GetOption<bool>(Renderer_Option::FilmGrain))
            {
                swap_output = !swap_output;
                Pass_FilmGrain(cmd_list, get_output_in, get_output_out);
            }
        }

        // if the last written texture is not the output one, then make sure it is.
        if (!swap_output)
        {
            cmd_list->Copy(rt_frame_output_scratch, rt_frame_output, false);
        }

        cmd_list->EndMarker();
    }

    void Renderer::Pass_Bloom(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_luminance        = GetShader(Renderer_Shader::bloom_luminance_c).get();
        RHI_Shader* shader_upsampleBlendMip = GetShader(Renderer_Shader::bloom_upsample_blend_mip_c).get();
        RHI_Shader* shader_blendFrame       = GetShader(Renderer_Shader::bloom_blend_frame_c).get();

        if (!shader_luminance->IsCompiled() || !shader_upsampleBlendMip->IsCompiled() || !shader_blendFrame->IsCompiled())
            return;

        cmd_list->BeginTimeblock("bloom");

        // Acquire render target
        RHI_Texture* tex_bloom = GetRenderTarget(Renderer_RenderTexture::bloom).get();

        // Luminance
        cmd_list->BeginMarker("luminance");
        {
            // Define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_luminance;

            // Set pipeline state
            cmd_list->SetPipelineState(pso);

            // Set pass constants
            m_cb_pass_cpu.set_resolution_out(tex_bloom);
            PushPassConstants(cmd_list);

            // Set textures
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_bloom);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

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

                // Set pass constants
                m_cb_pass_cpu.set_resolution_out(Vector2(static_cast<float>(mip_width_large), static_cast<float>(mip_height_height)));
                PushPassConstants(cmd_list);

                // Set textures
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_bloom, mip_index_small, 1);
                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_bloom, mip_index_big, 1);

                // Blend
                uint32_t thread_group_count_x_ = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(mip_width_large) / thread_group_count));
                uint32_t thread_group_count_y_ = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(mip_height_height) / thread_group_count));
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

            // Set pass constants
            m_cb_pass_cpu.set_resolution_out(tex_out);
            m_cb_pass_cpu.set_f3_value(GetOption<float>(Renderer_Option::Bloom), 0.0f, 0.0f);
            PushPassConstants(cmd_list);

            // Set textures
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_bloom, 0, 1);

            // Render
            cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));
        }
        cmd_list->EndMarker();

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_ToneMappingGammaCorrection(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // acquire shaders
        RHI_Shader* shader_c = GetShader(Renderer_Shader::tonemapping_gamma_correction_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock("tonemapping_gamma_correction");

        // define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // set pipeline state
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_cb_pass_cpu.set_resolution_out(tex_out);
        m_cb_pass_cpu.set_f3_value(Display::GetLuminanceMax(), GetOption<float>(Renderer_Option::Tonemapping), GetOption<float>(Renderer_Option::Exposure));
        m_cb_pass_cpu.set_f3_value2(GetOption<float>(Renderer_Option::Hdr), 0.0f, 0.0f);
        PushPassConstants(cmd_list);

        // set textures
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Fxaa(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = GetShader(Renderer_Shader::fxaa_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock("fxaa");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Set pass constants
        m_cb_pass_cpu.set_resolution_out(tex_out);
        PushPassConstants(cmd_list);

        // Set textures
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        
        // Render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_ChromaticAberration(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // acquire shaders
        RHI_Shader* shader_c = GetShader(Renderer_Shader::chromatic_aberration_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock("chromatic_aberration");

        // define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // set pipeline state
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_cb_pass_cpu.set_resolution_out(tex_out);
        m_cb_pass_cpu.set_f3_value(m_camera->GetAperture(), 0.0f, 0.0f);
        PushPassConstants(cmd_list);

        // set textures
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_MotionBlur(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // acquire shaders
        RHI_Shader* shader_c = GetShader(Renderer_Shader::motion_blur_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock("motion_blur");

        // define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // set pipeline state
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_cb_pass_cpu.set_resolution_out(tex_out);
        m_cb_pass_cpu.set_f3_value(m_camera->GetShutterSpeed(), 0.0f, 0.0f);
        PushPassConstants(cmd_list);

        // set textures
        BindTexturesGfbuffer(cmd_list);
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_DepthOfField(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // acquire shaders
        RHI_Shader* shader_downsampleCoc = GetShader(Renderer_Shader::dof_downsample_coc_c).get();
        RHI_Shader* shader_bokeh         = GetShader(Renderer_Shader::dof_bokeh_c).get();
        RHI_Shader* shader_tent          = GetShader(Renderer_Shader::dof_tent_c).get();
        RHI_Shader* shader_upsampleBlend = GetShader(Renderer_Shader::dof_upscale_blend_c).get();
        if (!shader_downsampleCoc->IsCompiled() || !shader_bokeh->IsCompiled() || !shader_tent->IsCompiled() || !shader_upsampleBlend->IsCompiled())
            return;

        cmd_list->BeginTimeblock("depth_of_field");

        // acquire render targets
        RHI_Texture* tex_bokeh_half   = GetRenderTarget(Renderer_RenderTexture::dof_half).get();
        RHI_Texture* tex_bokeh_half_2 = GetRenderTarget(Renderer_RenderTexture::dof_half_2).get();

        // downsample and compute circle of confusion
        cmd_list->BeginMarker("circle_of_confusion");
        {
            // define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_downsampleCoc;

            // set pipeline state
            cmd_list->SetPipelineState(pso);

            // set pass constants
            m_cb_pass_cpu.set_resolution_out(tex_bokeh_half);
            m_cb_pass_cpu.set_f3_value(m_camera->GetAperture(), 0.0f, 0.0f);
            PushPassConstants(cmd_list);

            // set textures
            BindTexturesGfbuffer(cmd_list);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_bokeh_half);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

            // Render
            cmd_list->Dispatch(thread_group_count_x(tex_bokeh_half), thread_group_count_y(tex_bokeh_half));
        }
        cmd_list->EndMarker();

        // bokeh
        cmd_list->BeginMarker("bokeh");
        {
            // define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_bokeh;

            // set pipeline state
            cmd_list->SetPipelineState(pso);

            // set pass constants
            m_cb_pass_cpu.set_resolution_out(tex_bokeh_half_2);
            m_cb_pass_cpu.set_f3_value(m_camera->GetAperture(), 0.0f, 0.0f);
            PushPassConstants(cmd_list);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_bokeh_half_2);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_bokeh_half);

            // render
            cmd_list->Dispatch(thread_group_count_x(tex_bokeh_half_2), thread_group_count_y(tex_bokeh_half_2));
        }
        cmd_list->EndMarker();

        // blur the bokeh using a tent filter
        cmd_list->BeginMarker("tent");
        {
            // define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_tent;

            // set pipeline state
            cmd_list->SetPipelineState(pso);

            // set pass constants
            m_cb_pass_cpu.set_resolution_out(tex_bokeh_half);
            m_cb_pass_cpu.set_f3_value(m_camera->GetAperture(), 0.0f, 0.0f);
            PushPassConstants(cmd_list);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_bokeh_half);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_bokeh_half_2);

            // render
            cmd_list->Dispatch(thread_group_count_x(tex_bokeh_half), thread_group_count_y(tex_bokeh_half));
        }
        cmd_list->EndMarker();

        // upscale & Blend
        cmd_list->BeginMarker("upsample_and_blend_with_frame");
        {
            // define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_upsampleBlend;

            // set pipeline state
            cmd_list->SetPipelineState(pso);

            // set pass constants
            m_cb_pass_cpu.set_resolution_out(tex_out);
            m_cb_pass_cpu.set_f3_value(m_camera->GetAperture(), 0.0f, 0.0f);
            PushPassConstants(cmd_list);

            // set textures
            BindTexturesGfbuffer(cmd_list);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_bokeh_half);

            // render
            cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));
        }
        cmd_list->EndMarker();

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Debanding(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader = GetShader(Renderer_Shader::debanding_c).get();
        if (!shader->IsCompiled())
            return;

        cmd_list->BeginTimeblock("debanding");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Set pass constants
        m_cb_pass_cpu.set_resolution_out(tex_out);
        PushPassConstants(cmd_list);

        // Set textures
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

        // Render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_FilmGrain(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // acquire shaders
        RHI_Shader* shader_c = GetShader(Renderer_Shader::film_grain_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock("film_grain");

        // define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // set pipeline state
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_cb_pass_cpu.set_resolution_out(tex_out);
        m_cb_pass_cpu.set_f3_value(m_camera->GetIso(), 0.0f, 0.0f);
        PushPassConstants(cmd_list);

        // set textures
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Ffx_Cas(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = GetShader(Renderer_Shader::ffx_cas_c).get();
        if (!shader_c->IsCompiled())
            return;

        float sharpness = GetOption<float>(Renderer_Option::Sharpness);

        if (sharpness != 0.0f)
        {
            cmd_list->BeginTimeblock("ffx_cas");

            // define pipeline state
            static RHI_PipelineState pso;
            pso.name           = "ffx_cas";
            pso.shader_compute = shader_c;

            // set pipeline state
            cmd_list->SetPipelineState(pso);

            // set pass constants
            m_cb_pass_cpu.set_resolution_out(tex_out);
            m_cb_pass_cpu.set_f3_value(sharpness, 0.0f, 0.0f);
            PushPassConstants(cmd_list);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

            // render
            cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

            cmd_list->EndTimeblock();
        }
    }

    void Renderer::Pass_Ffx_Spd(RHI_CommandList* cmd_list, RHI_Texture* tex)
    {
        // AMD FidelityFX Single Pass Downsampler.
        // Provides an RDNA™-optimized solution for generating up to 12 MIP levels of a texture.
        // GitHub:        https://github.com/GPUOpen-Effects/FidelityFX-SPD
        // Documentation: https://github.com/GPUOpen-Effects/FidelityFX-SPD/blob/master/docs/FidelityFX_SPD.pdf

        uint32_t output_mip_count = tex->GetMipCount() - 1;
        uint32_t smallest_width   = tex->GetWidth()  >> output_mip_count;
        uint32_t smallest_height  = tex->GetHeight() >> output_mip_count;

        // ensure that the input texture meets the requirements.
        SP_ASSERT(tex->HasPerMipViews());
        SP_ASSERT(tex->GetWidth() <= 4096 && tex->GetHeight() <= 4096 && output_mip_count <= 12); // As per documentation (page 22)

        // acquire shader
        RHI_Shader* shader_c = GetShader(Renderer_Shader::ffx_spd_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginMarker("ffx_spd");

        // define render state
        static RHI_PipelineState pso;
        pso.name           = "ffx_spd";
        pso.shader_compute = shader_c;

        // set pipeline state
        cmd_list->SetPipelineState(pso);

        // as per documentation (page 22)
        const uint32_t thread_group_count_x_ = (tex->GetWidth() + 63) >> 6;
        const uint32_t thread_group_count_y_ = (tex->GetHeight() + 63) >> 6;

        // push pass data
        m_cb_pass_cpu.set_resolution_out(tex);
        m_cb_pass_cpu.set_f3_value(static_cast<float>(output_mip_count), static_cast<float>(thread_group_count_x_ * thread_group_count_y_), 0.0f);
        PushPassConstants(cmd_list);

        // update counter
        uint32_t counter_value = 0;
        GetStructuredBuffer()->Update(&counter_value);
        cmd_list->SetStructuredBuffer(Renderer_BindingsUav::atomic_counter, GetStructuredBuffer());

        // set textures
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex, 0, 1);                            // top mip
        cmd_list->SetTexture(Renderer_BindingsUav::tex_array, tex, 1, tex->GetMipCount() - 1); // rest of the mips

        // render
        cmd_list->Dispatch(thread_group_count_x_, thread_group_count_y_);
        cmd_list->EndMarker();
    }

    void Renderer::Pass_Ffx_Fsr2(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        cmd_list->BeginTimeblock("amd_ffx_fsr2");

        bool is_upsampling = GetResolutionRender().x < GetResolutionOutput().x || GetResolutionRender().y < GetResolutionOutput().y;
        float sharpness    = is_upsampling ? GetOption<float>(Renderer_Option::Sharpness) : 0.0f; // if not upsampling we do Pass_Ffx_Cas()

        RHI_AMD_FidelityFX::FSR2_Dispatch(
            cmd_list,
            tex_in,
            GetRenderTarget(Renderer_RenderTexture::gbuffer_depth).get(),
            GetRenderTarget(Renderer_RenderTexture::gbuffer_velocity).get(),
            GetRenderTarget(Renderer_RenderTexture::fsr2_mask_reactive).get(),
            GetRenderTarget(Renderer_RenderTexture::fsr2_mask_transparency).get(),
            tex_out,
            GetCamera().get(),
            m_cb_frame_cpu.delta_time,
            sharpness,
            GetOption<float>(Renderer_Option::Exposure)
        );

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Icons(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (!GetOption<bool>(Renderer_Option::Debug_Lights) || Engine::IsFlagSet(EngineMode::Game))
            return;

        // acquire shaders
        RHI_Shader* shader_v = GetShader(Renderer_Shader::quad_v).get();
        RHI_Shader* shader_p = GetShader(Renderer_Shader::quad_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // acquire entities
        auto& lights        = m_renderables[Renderer_Entity::Light];
        auto& audio_sources = m_renderables[Renderer_Entity::AudioSource];
        if ((lights.empty() && audio_sources.empty()) || !GetCamera())
            return;

        cmd_list->BeginTimeblock("icons");

        // define pipeline state
        static RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v;
        pso.shader_pixel                    = shader_p;
        pso.rasterizer_state                = GetRasterizerState(Renderer_RasterizerState::Solid_cull_back).get();
        pso.blend_state                     = GetBlendState(Renderer_BlendState::Alpha).get();
        pso.depth_stencil_state             = GetDepthStencilState(Renderer_DepthStencilState::Off).get();
        pso.render_target_color_textures[0] = tex_out;
        pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;

        // set pipeline state
        cmd_list->SetPipelineState(pso);

        bool render_pass_started = false;
        auto draw_icon = [&cmd_list, &render_pass_started](Transform* transform, RHI_Texture* texture)
        {
            const Vector3 pos_world        = transform->GetPosition();
            const Vector3 pos_world_camera = GetCamera()->GetTransform()->GetPosition();
            const Vector3 camera_to_light  = (pos_world - pos_world_camera).Normalized();
            const float v_dot_l            = Vector3::Dot(GetCamera()->GetTransform()->GetForward(), camera_to_light);

            // only draw if it's inside our view
            if (v_dot_l > 0.5f)
            {
                if (!render_pass_started)
                {
                    cmd_list->BeginRenderPass();
                    render_pass_started = true;
                }

                // compute transform
                {
                    // Use the distance from the camera to scale the icon, this will
                    // cancel out perspective scaling, hence keeping the icon scale constant.
                    const float distance = (pos_world_camera - pos_world).Length();
                    const float scale = distance * 0.04f;

                    // 1st rotation: The quad's normal is parallel to the world's Y axis, so we rotate to make it camera facing.
                    Quaternion rotation_reorient_quad = Quaternion::FromEulerAngles(-90.0f, 0.0f, 0.0f);
                    // 2nd rotation: Rotate the camera facing quad with the camera, so that it remains a camera facing quad.
                    Quaternion rotation_camera_billboard = Quaternion::FromLookRotation(pos_world - pos_world_camera);

                    Matrix transform = Matrix(pos_world, rotation_camera_billboard * rotation_reorient_quad, scale);

                    // set transform
                    m_cb_pass_cpu.transform = transform * m_cb_frame_cpu.view_projection;
                    PushPassConstants(cmd_list);
                }

                // draw rectangle
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, texture);
                cmd_list->SetBufferVertex(GetStandardMesh(Renderer_MeshType::Quad)->GetVertexBuffer());
                cmd_list->SetBufferIndex(GetStandardMesh(Renderer_MeshType::Quad)->GetIndexBuffer());
                cmd_list->DrawIndexed(6);
            }
        };

        // draw audio source icons
        for (shared_ptr<Entity> entity : audio_sources)
        {
            draw_icon(entity->GetTransform().get(), GetStandardTexture(Renderer_StandardTexture::Gizmo_audio_source).get());
        }

        // draw light icons
        for (shared_ptr<Entity> entity : lights)
        {
            RHI_Texture* texture = nullptr;

            // light can be null if it just got removed and our buffer doesn't update till the next frame
            if (shared_ptr<Light> light = entity->GetComponent<Light>())
            {
                // get the texture
                if (light->GetLightType() == LightType::Directional) texture = GetStandardTexture(Renderer_StandardTexture::Gizmo_light_directional).get();
                else if (light->GetLightType() == LightType::Point)  texture = GetStandardTexture(Renderer_StandardTexture::Gizmo_light_point).get();
                else if (light->GetLightType() == LightType::Spot)   texture = GetStandardTexture(Renderer_StandardTexture::Gizmo_light_spot).get();
            }

            draw_icon(entity->GetTransform().get(), texture);
        }

        if (render_pass_started)
        {
            cmd_list->EndRenderPass();
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Grid(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (!GetOption<bool>(Renderer_Option::Debug_Grid))
            return;

        // acquire shader
        RHI_Shader* shader_v = GetShader(Renderer_Shader::quad_v).get();
        RHI_Shader* shader_p = GetShader(Renderer_Shader::grid_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;      

        // define the pipeline state
        static RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v;
        pso.shader_pixel                    = shader_p;
        pso.rasterizer_state                = GetRasterizerState(Renderer_RasterizerState::Solid_cull_none).get();
        pso.blend_state                     = GetBlendState(Renderer_BlendState::Alpha).get();
        pso.depth_stencil_state             = GetDepthStencilState(Renderer_DepthStencilState::Depth_read).get();
        pso.render_target_color_textures[1] = GetRenderTarget(Renderer_RenderTexture::fsr2_mask_reactive).get();
        pso.clear_color[1]                  = rhi_color_load;
        pso.render_target_color_textures[0] = tex_out;
        pso.render_target_depth_texture     = GetRenderTarget(Renderer_RenderTexture::gbuffer_depth).get();
        pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;

        // draw
        cmd_list->BeginTimeblock("grid");
        cmd_list->SetPipelineState(pso);
        cmd_list->BeginRenderPass();

        // set transform
        {
            // calculate grid spacing and translation to simulate an infinite grid
            const float grid_spacing       = 1.0f;
            const Vector3& camera_position = m_camera->GetTransform()->GetPosition();
            const Vector3 translation      = Vector3(
                floor(camera_position.x / grid_spacing) * grid_spacing,
                0.0f,
                floor(camera_position.z / grid_spacing) * grid_spacing
            );
            Matrix quad_transform   = Matrix::CreateScale(Vector3(1000.0f, 1.0f, 1000.0f)) * Matrix::CreateTranslation(translation);
            m_cb_pass_cpu.transform = quad_transform * m_cb_frame_cpu.view_projection_unjittered;

            // style
            const float line_internval  = 0.001f;
            const float line_thickeness = 0.00001f;
            m_cb_pass_cpu.set_f3_value(line_internval, line_thickeness, 0.0f);

            PushPassConstants(cmd_list);
        }

        cmd_list->SetBufferVertex(GetStandardMesh(Renderer_MeshType::Quad)->GetVertexBuffer());
        cmd_list->SetBufferIndex(GetStandardMesh(Renderer_MeshType::Quad)->GetIndexBuffer());
        cmd_list->DrawIndexed(6);

        cmd_list->EndRenderPass();
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Lines(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        // acquire shaders
        RHI_Shader* shader_v = GetShader(Renderer_Shader::line_v).get();
        RHI_Shader* shader_p = GetShader(Renderer_Shader::line_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        cmd_list->BeginTimeblock("lines");

        // define the pipeline state
        static RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v;
        pso.shader_pixel                    = shader_p;
        pso.rasterizer_state                = GetRasterizerState(Renderer_RasterizerState::Wireframe_cull_none).get();
        pso.render_target_color_textures[0] = tex_out;
        pso.clear_color[0]                  = rhi_color_load;
        pso.render_target_color_textures[1] = GetRenderTarget(Renderer_RenderTexture::fsr2_mask_reactive).get();
        pso.clear_color[1]                  = rhi_color_load;
        pso.render_target_depth_texture     = GetRenderTarget(Renderer_RenderTexture::gbuffer_depth).get();
        pso.primitive_topology              = RHI_PrimitiveTopology_Mode::LineList;

        // world space rendering
        m_cb_pass_cpu.transform = Matrix::Identity;
        PushPassConstants(cmd_list);

        // draw independent lines
        const bool draw_lines_depth_off = m_lines_index_depth_off != numeric_limits<uint32_t>::max();
        const bool draw_lines_depth_on  = m_lines_index_depth_on > ((m_line_vertices.size() / 2) - 1);
        if (draw_lines_depth_off || draw_lines_depth_on)
        {
            // grow vertex buffer (if needed)
            uint32_t vertex_count = static_cast<uint32_t>(m_line_vertices.size());
            if (vertex_count > m_vertex_buffer_lines->GetVertexCount())
            {
                m_vertex_buffer_lines->CreateDynamic<RHI_Vertex_PosCol>(vertex_count);
            }

            // if the vertex count is 0, the vertex buffer will be uninitialised.
            if (vertex_count != 0)
            {
                // update vertex buffer
                RHI_Vertex_PosCol* buffer = static_cast<RHI_Vertex_PosCol*>(m_vertex_buffer_lines->GetMappedData());
                copy(m_line_vertices.begin(), m_line_vertices.end(), buffer);

                // depth off
                if (draw_lines_depth_off)
                {
                    cmd_list->BeginMarker("depth_off");

                    // set pipeline state
                    pso.blend_state         = GetBlendState(Renderer_BlendState::Disabled).get();
                    pso.depth_stencil_state = GetDepthStencilState(Renderer_DepthStencilState::Off).get();

                    cmd_list->SetPipelineState(pso);
                    cmd_list->BeginRenderPass();
                    cmd_list->SetBufferVertex(m_vertex_buffer_lines.get());
                    cmd_list->Draw(m_lines_index_depth_off + 1);
                    cmd_list->EndRenderPass();
                    cmd_list->EndMarker();
                }

                // depth on
                if (m_lines_index_depth_on > (vertex_count / 2) - 1)
                {
                    cmd_list->BeginMarker("depth_on");

                    // set pipeline state
                    pso.blend_state         = GetBlendState(Renderer_BlendState::Alpha).get();
                    pso.depth_stencil_state = GetDepthStencilState(Renderer_DepthStencilState::Depth_read).get();

                    cmd_list->SetPipelineState(pso);
                    cmd_list->BeginRenderPass();
                    cmd_list->SetBufferVertex(m_vertex_buffer_lines.get());
                    cmd_list->Draw((m_lines_index_depth_on - (vertex_count / 2)) + 1, vertex_count / 2);
                    cmd_list->EndRenderPass();
                    cmd_list->EndMarker();
                }
            }
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_DebugMeshes(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (!GetOption<bool>(Renderer_Option::Debug_ReflectionProbes))
            return;

        // Acquire color shaders.
        RHI_Shader* shader_v = GetShader(Renderer_Shader::debug_reflection_probe_v).get();
        RHI_Shader* shader_p = GetShader(Renderer_Shader::debug_reflection_probe_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Get reflection probe entities
        const vector<shared_ptr<Entity>>& probes = m_renderables[Renderer_Entity::ReflectionProbe];
        if (probes.empty())
            return;

        cmd_list->BeginTimeblock("debug_meshes");

        // Define pipeline state
        static RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v;
        pso.shader_pixel                    = shader_p;
        pso.rasterizer_state                = GetRasterizerState(Renderer_RasterizerState::Solid_cull_back).get();
        pso.blend_state                     = GetBlendState(Renderer_BlendState::Disabled).get();
        pso.depth_stencil_state             = GetDepthStencilState(Renderer_DepthStencilState::Depth_read).get();
        pso.render_target_color_textures[0] = tex_out;
        pso.render_target_depth_texture     = GetRenderTarget(Renderer_RenderTexture::gbuffer_depth).get();
        pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Render
        cmd_list->BeginRenderPass();
        {
            cmd_list->SetBufferVertex(GetStandardMesh(Renderer_MeshType::Sphere)->GetVertexBuffer());
            cmd_list->SetBufferIndex(GetStandardMesh(Renderer_MeshType::Sphere)->GetIndexBuffer());

            for (uint32_t probe_index = 0; probe_index < static_cast<uint32_t>(probes.size()); probe_index++)
            {
                if (shared_ptr<ReflectionProbe> probe = probes[probe_index]->GetComponent<ReflectionProbe>())
                {
                    // Set pass constants
                    m_cb_pass_cpu.transform = probe->GetTransform()->GetMatrix();
                    PushPassConstants(cmd_list);

                    cmd_list->SetTexture(Renderer_BindingsSrv::reflection_probe, probe->GetColorTexture());
                    cmd_list->DrawIndexed(GetStandardMesh(Renderer_MeshType::Sphere)->GetIndexCount());

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
        if (!GetOption<bool>(Renderer_Option::Debug_SelectionOutline) || Engine::IsFlagSet(EngineMode::Game))
            return;

        // acquire shaders
        RHI_Shader* shader_v = GetShader(Renderer_Shader::outline_v).get();
        RHI_Shader* shader_p = GetShader(Renderer_Shader::outline_p).get();
        RHI_Shader* shader_c = GetShader(Renderer_Shader::outline_c).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled() || !shader_c->IsCompiled())
            return;

        if (shared_ptr<Camera> camera = Renderer::GetCamera())
        {
            if (shared_ptr<Entity> entity_selected = camera->GetSelectedEntity())
            {
                cmd_list->BeginTimeblock("outline");
                {
                    RHI_Texture* tex_outline = GetRenderTarget(Renderer_RenderTexture::outline).get();
                    static const Color clear_color = Color(0.0f, 0.0f, 0.0f, 0.0f);

                    if (shared_ptr<Renderable> renderable = entity_selected->GetComponent<Renderable>())
                    {
                        if (Mesh* mesh = renderable->GetMesh())
                        {
                            if (mesh->GetVertexBuffer() && mesh->GetIndexBuffer())
                            {
                                cmd_list->BeginMarker("color_silhouette");
                                {
                                    // Define render state
                                    static RHI_PipelineState pso;
                                    pso.shader_vertex                   = shader_v;
                                    pso.shader_pixel                    = shader_p;
                                    pso.rasterizer_state                = GetRasterizerState(Renderer_RasterizerState::Solid_cull_back).get();
                                    pso.blend_state                     = GetBlendState(Renderer_BlendState::Disabled).get();
                                    pso.depth_stencil_state             = GetDepthStencilState(Renderer_DepthStencilState::Off).get();
                                    pso.render_target_color_textures[0] = tex_outline;
                                    pso.clear_color[0]                  = clear_color;
                                    pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;

                                    // Set pipeline state
                                    cmd_list->SetPipelineState(pso);

                                    // Render
                                    cmd_list->BeginRenderPass();
                                    {
                                        // push draw data
                                        m_cb_pass_cpu.set_f4_value(debug_color);
                                        m_cb_pass_cpu.transform = entity_selected->GetTransform()->GetMatrix();
                                        PushPassConstants(cmd_list);

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

                                    // Set pass constants
                                    m_cb_pass_cpu.set_resolution_out(tex_out);
                                    PushPassConstants(cmd_list);

                                    // Set textures
                                    cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
                                    cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_outline);

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

    void Renderer::Pass_Text(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        // early exit cases
        const bool draw       = GetOption<bool>(Renderer_Option::Debug_PerformanceMetrics);
        const auto& shader_v  = GetShader(Renderer_Shader::font_v);
        const auto& shader_p  = GetShader(Renderer_Shader::font_p);
        shared_ptr<Font> font = GetFont();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled() || !draw || !font->HasText())
            return;

        // if the performance metrics are being drawn, the profiler has to be enabled.
        if (!Profiler::GetEnabled())
        {
            Profiler::SetEnabled(true);
        }

        cmd_list->BeginMarker("text");

        // define pipeline state
        static RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v.get();
        pso.shader_pixel                    = shader_p.get();
        pso.rasterizer_state                = GetRasterizerState(Renderer_RasterizerState::Solid_cull_back).get();
        pso.blend_state                     = GetBlendState(Renderer_BlendState::Alpha).get();
        pso.depth_stencil_state             = GetDepthStencilState(Renderer_DepthStencilState::Off).get();
        pso.render_target_color_textures[0] = tex_out;
        pso.clear_color[0]                  = rhi_color_load;
        pso.primitive_topology              = RHI_PrimitiveTopology_Mode::TriangleList;
        pso.name                            = "Pass_Text";

        font->UpdateVertexAndIndexBuffers();

        // outline
        if (font->GetOutline() != Font_Outline_None && font->GetOutlineSize() != 0)
        {
            cmd_list->BeginTimeblock("outline");
            cmd_list->SetPipelineState(pso);
            cmd_list->BeginRenderPass();
            {
                // set pass constants
                m_cb_pass_cpu.set_resolution_out(tex_out);
                m_cb_pass_cpu.set_f4_value(font->GetColorOutline());
                PushPassConstants(cmd_list);

                cmd_list->SetBufferVertex(font->GetVertexBuffer());
                cmd_list->SetBufferIndex(font->GetIndexBuffer());
                cmd_list->SetTexture(Renderer_BindingsSrv::font_atlas, font->GetAtlasOutline());
                cmd_list->DrawIndexed(font->GetIndexCount());
            }
            cmd_list->EndRenderPass();
            cmd_list->EndTimeblock();
        }

        // inline
        {
            cmd_list->BeginTimeblock("inline");
            cmd_list->SetPipelineState(pso);
            cmd_list->BeginRenderPass();
            {
                // set pass constants
                m_cb_pass_cpu.set_resolution_out(tex_out);
                m_cb_pass_cpu.set_f4_value(font->GetColor());
                PushPassConstants(cmd_list);

                cmd_list->SetBufferVertex(font->GetVertexBuffer());
                cmd_list->SetBufferIndex(font->GetIndexBuffer());
                cmd_list->SetTexture(Renderer_BindingsSrv::font_atlas, font->GetAtlas());
                cmd_list->DrawIndexed(font->GetIndexCount());
                cmd_list->EndRenderPass();
            }
            cmd_list->EndTimeblock();
        }

        cmd_list->EndMarker();
    }

    void Renderer::Pass_BrdfSpecularLut(RHI_CommandList* cmd_list)
    {
        // acquire shader
        RHI_Shader* shader_c = GetShader(Renderer_Shader::brdf_specular_lut_c).get();
        if (!shader_c->IsCompiled())
            return;

        // acquire render target
        RHI_Texture* tex_brdf_specular_lut = GetRenderTarget(Renderer_RenderTexture::brdf_specular_lut).get();

        // define render state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        cmd_list->BeginTimeblock("brdf_specular_lut");

        // set pipeline state
        cmd_list->SetPipelineState(pso);

        // Set pass constants
        m_cb_pass_cpu.set_resolution_out(tex_brdf_specular_lut);
        PushPassConstants(cmd_list);

        // set texture
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_brdf_specular_lut);

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_brdf_specular_lut), thread_group_count_y(tex_brdf_specular_lut));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_GenerateMips(RHI_CommandList* cmd_list, RHI_Texture* texture)
    {
        SP_ASSERT(texture != nullptr);
        SP_ASSERT(texture->GetRhiSrv() != nullptr);
        SP_ASSERT(texture->HasMips());        // ensure the texture has mips (of course, they are empty at this point)
        SP_ASSERT(texture->HasPerMipViews()); // ensure that the texture has per mip views since they are required for GPU downsampling.
        SP_ASSERT(texture->IsReadyForUse());  // ensure that any loading and resource creation has finished

        lock_guard<mutex> lock(mutex_generate_mips);

        // downsample
        Pass_Ffx_Spd(cmd_list, texture);

        // set all generated mips to read only optimal
        texture->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list, 0, texture->GetMipCount());

        // destroy per mip resource views since they are no longer needed
        {
            // remove unnecessary flags from texture (were only needed for the downsampling)
            uint32_t flags = texture->GetFlags();
            flags &= ~RHI_Texture_PerMipViews;
            flags &= ~RHI_Texture_Uav;
            texture->SetFlags(flags);

            // destroy the resources associated with those flags
            {
                const bool destroy_main     = false;
                const bool destroy_per_view = true;
                texture->RHI_DestroyResource(destroy_main, destroy_per_view);
            }
        }
    }
}
