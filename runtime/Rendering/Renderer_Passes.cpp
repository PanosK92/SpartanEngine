#/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES ===============================
#include "pch.h"
#include "Renderer.h"
#include "../Profiling/Profiler.h"
#include "../World/Entity.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Components/AudioSource.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Buffer.h"
#include "../RHI/RHI_Shader.h"
#include "../Rendering/Material.h"
#include "../RHI/RHI_VendorTechnology.h"
#include "../RHI/RHI_RasterizerState.h"
SP_WARNINGS_OFF
#include "bend_sss_cpu.h"
#include "../RHI/RHI_OpenImageDenoise.h"
SP_WARNINGS_ON
//==========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    array<Renderer_DrawCall, renderer_max_entities> Renderer::m_draw_calls;
    uint32_t Renderer::m_draw_call_count;

    void Renderer::SetStandardResources(RHI_CommandList* cmd_list)
    {
        cmd_list->SetConstantBuffer(Renderer_BindingsCb::frame, GetBuffer(Renderer_Buffer::ConstantFrame));
    }

    void Renderer::ProduceFrame(RHI_CommandList* cmd_list_present, RHI_CommandList* cmd_list_graphics_secondary)
    {
        SP_PROFILE_CPU();

        // early exit if one or more shaders aren't ready
        for (const auto& shader : GetShaders())
        {
            if (!shader || !shader->IsCompiled())
                return;
        }

        // acquire render targets
        RHI_Texture* rt_render = GetRenderTarget(Renderer_RenderTarget::frame_render);
        RHI_Texture* rt_output = GetRenderTarget(Renderer_RenderTarget::frame_output);

        // brdf specular lut
        static bool brdf_specular_lut_produced = false;
        if (!brdf_specular_lut_produced)
        {
            Pass_Light_Integration_BrdfSpecularLut(cmd_list_present);
            brdf_specular_lut_produced = true;
        }

        if (Camera* camera = World::GetCamera())
        {
            Pass_VariableRateShading(cmd_list_present);

            // opaques
            {
                bool is_transparent = false;
                //Pass_Occlusion(cmd_list_graphics_secondary);
                Pass_Depth_Prepass(cmd_list_present);
                Pass_GBuffer(cmd_list_present, is_transparent);
                Pass_ShadowMaps(cmd_list_present);
                Pass_Skysphere(cmd_list_present);
                Pass_Sss(cmd_list_present);
                Pass_Ssao(cmd_list_present);
                Pass_Light(cmd_list_present, is_transparent);             // compute diffuse and specular buffers
                Pass_Light_GlobalIllumination(cmd_list_present);          // compute global illumination
                Pass_Light_Composition(cmd_list_present, is_transparent); // compose all light (diffuse, specular, etc).
            }

            // transparents
            if (m_transparents_present)
            {
                bool is_transparent = true;
                Pass_GBuffer(cmd_list_present, is_transparent);
                Pass_Light(cmd_list_present, is_transparent);
                Pass_Light_Composition(cmd_list_present, is_transparent);
            }

            // apply skysphere, ssr and global illumination
            Pass_Ssr(cmd_list_present);
            Pass_Light_ImageBased(cmd_list_present);

            // render -> output resolution
            Pass_Upscale(cmd_list_present);

            // post-process
            {
                // game
                Pass_PostProcess(cmd_list_present);

                // editor
                Pass_Grid(cmd_list_present, rt_output);
                Pass_Lines(cmd_list_present, rt_output);
                Pass_Outline(cmd_list_present, rt_output);
                Pass_Icons(cmd_list_present, rt_output);
            }
        }
        else
        {
            cmd_list_present->ClearTexture(rt_output, Color::standard_black);
        }

        Pass_Text(cmd_list_present, rt_output);

        // perform early transitions (so the next frame doesn't have to wait)
        rt_output->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list_present);
        GetRenderTarget(Renderer_RenderTarget::gbuffer_color)->SetLayout(RHI_Image_Layout::Attachment, cmd_list_present);
        GetRenderTarget(Renderer_RenderTarget::gbuffer_normal)->SetLayout(RHI_Image_Layout::Attachment, cmd_list_present);
        GetRenderTarget(Renderer_RenderTarget::gbuffer_material)->SetLayout(RHI_Image_Layout::Attachment, cmd_list_present);
        GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity)->SetLayout(RHI_Image_Layout::Attachment, cmd_list_present);
        GetRenderTarget(Renderer_RenderTarget::gbuffer_depth)->SetLayout(RHI_Image_Layout::Attachment, cmd_list_present);
    }

    void Renderer::Pass_VariableRateShading(RHI_CommandList* cmd_list)
    {
        if (!GetOption<bool>(Renderer_Option::VariableRateShading))
            return;

        // acquire resources
        RHI_Shader* shader_c = GetShader(Renderer_Shader::variable_rate_shading_c);
        RHI_Texture* tex_in  = GetRenderTarget(Renderer_RenderTarget::frame_output);
        RHI_Texture* tex_out = GetRenderTarget(Renderer_RenderTarget::shading_rate);

        cmd_list->BeginTimeblock("variable_rate_shading");
        {
            // set pipeline state
            RHI_PipelineState pso;
            pso.name             = "variable_rate_shading";
            pso.shaders[Compute] = shader_c;
            cmd_list->SetPipelineState(pso);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);

            // render
            cmd_list->Dispatch(tex_out);
        }
        cmd_list->EndTimeblock();
    }
  
    void Renderer::Pass_ShadowMaps(RHI_CommandList* cmd_list)
    {
        if (World::GetLightCount() == 0)
            return;

        cmd_list->BeginTimeblock("shadow_maps");
        {
            // define base pipeline state
            RHI_PipelineState pso;
            pso.name                             = "shadow_maps";
            pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::depth_light_v);
            pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
            pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
            pso.clear_depth                      = 0.0f;

            // constants
            constexpr size_t MAX_LIGHTS = 64;
            constexpr size_t MAX_RENDERABLES = 1024;
    
            // track which lights need updates and their visible renderables
            static array<bool, MAX_LIGHTS> update_shadow_map = { false };
            static array<array<Entity*, MAX_RENDERABLES>, MAX_LIGHTS * 6> visible_renderables_per_cascade = { nullptr }; // 6 for max cascades/faces (e.g., point light cubemap)
            static array<array<size_t, MAX_LIGHTS>, 6> visible_counts = { 0 }; // count per cascade per light
    
            // reset arrays
            fill(update_shadow_map.begin(), update_shadow_map.end(), false);
            for (size_t i = 0; i < MAX_LIGHTS * 6; ++i)
            {
                fill(visible_renderables_per_cascade[i].begin(), visible_renderables_per_cascade[i].end(), nullptr);
            }
            fill(visible_counts.begin(), visible_counts.end(), array<size_t, MAX_LIGHTS>{0});
    
            // get light entities
            const auto& light_entities = World::GetEntitiesLights();
            size_t light_count = min(light_entities.size(), MAX_LIGHTS);
    
            // single loop to compute updates and collect renderables
            for (const shared_ptr<Entity>& entity : World::GetEntities())
            {
                if (!entity->IsActive())
                    continue;
    
                Renderable* renderable = entity->GetComponent<Renderable>();
                if (!renderable || !renderable->HasFlag(RenderableFlags::CastsShadows))
                    continue;
    
                Material* material = renderable->GetMaterial();
                if (!material || material->IsTransparent())
                    continue;
    
                uint64_t current_lights = 0;
                uint32_t light_index    = 0;
    
                // check each light
                for (size_t i = 0; i < light_count && i < light_entities.size(); ++i)
                {
                    Light* light = light_entities[i]->GetComponent<Light>();
                    if (!light->GetFlag(LightFlags::Shadows))
                        continue;
    
                    uint32_t depth = light->GetDepthTexture()->GetDepth();
                    bool affects_light = false;
    
                    // check visibility per cascade/face
                    for (uint32_t array_index = 0; array_index < depth; ++array_index)
                    {
                        bool is_visible = false;
                        if (renderable->HasInstancing())
                        {
                            for (uint32_t group_index = 0; group_index < renderable->GetInstanceGroupCount(); ++group_index)
                            {
                                if (light->IsInViewFrustum(renderable, array_index, group_index))
                                {
                                    is_visible = true;
                                    break;
                                }
                            }
                        }
                        else
                        {
                            if (light->IsInViewFrustum(renderable, array_index))
                            {
                                is_visible = true;
                            }
                        }
    
                        if (is_visible)
                        {
                            affects_light       = true;
                            size_t cascade_slot = i * 6 + array_index; // unique slot for light and cascade
                            if (visible_counts[array_index][i] < MAX_RENDERABLES)
                            {
                                visible_renderables_per_cascade[cascade_slot][visible_counts[array_index][i]++] = entity.get();
                            }
                        }
                    }

                    bool is_dirty = light->GetFlag(LightFlags::ShadowDirty);
                    is_dirty      = entity->GetTimeSinceLastTransform() <= 0.25f                                       ? true : is_dirty;
                    is_dirty      = renderable->GetMaterial()->GetProperty(MaterialProperty::WindAnimation) > 0.0f ? true : is_dirty;
                    is_dirty      = light->GetLightType() == LightType::Directional                                    ? true : is_dirty;
                    if (affects_light && is_dirty)
                    {
                        current_lights       |= (1ULL << light_index);
                        update_shadow_map[i]  = true;
                        light->SetFlag(LightFlags::ShadowDirty, false);
                    }
    
                    light_index++;
                }
    
                renderable->SetPreviousLights(current_lights);
            }
    
            // render shadow maps using cached renderables
            for (size_t i = 0; i < light_count && i < light_entities.size(); ++i)
            {
                Light* light = light_entities[i]->GetComponent<Light>();
                if (!light->GetFlag(LightFlags::Shadows) || light->GetIntensityWatt() == 0.0f || !update_shadow_map[i])
                    continue;
    
                // set light-specific pso properties
                pso.render_target_depth_texture = light->GetDepthTexture();
                pso.rasterizer_state            = (light->GetLightType() == LightType::Directional) ? GetRasterizerState(Renderer_RasterizerState::Light_directional) : GetRasterizerState(Renderer_RasterizerState::Light_point_spot);

                // iterate over cascades/faces
                for (uint32_t array_index = 0; array_index < pso.render_target_depth_texture->GetDepth(); array_index++)
                {
                    pso.render_target_array_index = array_index;

                    // use cached renderables
                    size_t cascade_slot       = i * 6 + array_index;
                    size_t visible_count      = visible_counts[array_index][i];
                    auto& visible_renderables = visible_renderables_per_cascade[cascade_slot];
                    if (visible_count == 0)
                        continue;
    
                    // render cached renderables
                    for (size_t j = 0; j < visible_count; ++j)
                    {
                        Entity* entity         = visible_renderables[j];
                        Renderable* renderable = entity->GetComponent<Renderable>();
                        Material* material     = renderable->GetMaterial();
                        if (!material || material->IsTransparent())
                            continue;
    
                        // set per-renderable states
                        cmd_list->SetCullMode(static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)));
                        if (light->GetLightType() == LightType::Directional && array_index > 0)
                        {
                            pso.shaders[RHI_Shader_Type::Pixel] = nullptr; // no pixel shader for directional light cascades beyond index 0
                        }
                        else
                        {
                            pso.shaders[RHI_Shader_Type::Pixel] = material->IsAlphaTested() ? GetShader(Renderer_Shader::depth_light_alpha_color_p) : nullptr;
                        }
                        cmd_list->SetPipelineState(pso);
    
                        // set push constants
                        m_pcb_pass_cpu.set_f3_value2(static_cast<float>(light->GetIndex()), static_cast<float>(array_index), 0.0f);
                        m_pcb_pass_cpu.transform = renderable->GetEntity()->GetMatrix();
                        m_pcb_pass_cpu.set_f3_value(material->HasTextureOfType(MaterialTextureType::Color) ? 1.0f : 0.0f);
                        m_pcb_pass_cpu.set_is_transparent_and_material_index(false, material->GetIndex());
                        cmd_list->PushConstants(m_pcb_pass_cpu);
    
                        // set buffers
                        cmd_list->SetBufferVertex(renderable->GetVertexBuffer(), renderable->GetInstanceBuffer());
                        cmd_list->SetBufferIndex(renderable->GetIndexBuffer());
    
                        // draw
                        if (renderable->HasInstancing())
                        {
                            for (uint32_t group_index = 0; group_index < renderable->GetInstanceGroupCount(); group_index++)
                            {
                                uint32_t instance_index = renderable->GetInstanceGroupStartIndex(group_index);
                                uint32_t instance_count = renderable->GetInstanceGroupCount(group_index);
                                instance_count          = min(instance_count, renderable->GetInstanceCount() - instance_index);
                                if (instance_count == 0)
                                    continue;
                                
                                uint32_t lod_index = renderable->GetLodCount() - 1;
                                
                                cmd_list->DrawIndexed(
                                    renderable->GetIndexCount(lod_index),
                                    renderable->GetIndexOffset(lod_index),
                                    renderable->GetVertexOffset(lod_index),
                                    instance_index,
                                    instance_count
                                );
                            }
                        }
                        else
                        {
                            uint32_t lod_index = renderable->GetLodCount() - 1;
    
                            cmd_list->DrawIndexed(
                                renderable->GetIndexCount(lod_index),
                                renderable->GetIndexOffset(lod_index),
                                renderable->GetVertexOffset(lod_index)
                            );
                        }
                    }
                }
            }
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Occlusion(RHI_CommandList* cmd_list)
    {
        cmd_list->Begin();

        cmd_list->BeginTimeblock("occlusion");
        {
            // get resources
            RHI_Texture* tex_occluders     = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_occluders);
            RHI_Texture* tex_occluders_hiz = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_occluders_hiz);

            // occluders
            {
                // set pipeline state for depth-only rendering
                RHI_PipelineState pso;
                pso.name                             = "occluders";
                pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::depth_prepass_v);
                pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Solid);
                pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
                pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
                pso.render_target_depth_texture      = tex_occluders;
                pso.resolution_scale                 = true;
                pso.clear_depth                      = 0.0f;

                bool pipeline_set = false;
                for (uint32_t i = 0; i < m_draw_call_count; i++)
                {
                    const Renderer_DrawCall& draw_call = m_draw_calls[i];
                    if (!draw_call.is_occluder)
                        continue;

                    if (!pipeline_set)
                    {
                        cmd_list->SetPipelineState(pso);
                        pipeline_set = true;
                    }

                    // culling
                    Renderable* renderable = draw_call.renderable;
                    RHI_CullMode cull_mode = static_cast<RHI_CullMode>(renderable->GetMaterial()->GetProperty(MaterialProperty::CullMode));
                    cull_mode              = (pso.rasterizer_state->GetPolygonMode() == RHI_PolygonMode::Wireframe) ? RHI_CullMode::None : cull_mode;
                    cmd_list->SetCullMode(cull_mode);
    
                    // set pass constants
                    m_pcb_pass_cpu.transform = renderable->GetEntity()->GetMatrix();
                    cmd_list->PushConstants(m_pcb_pass_cpu);
    
                    // draw
                    cmd_list->SetBufferVertex(renderable->GetVertexBuffer());
                    cmd_list->SetBufferIndex(renderable->GetIndexBuffer());
    
                    cmd_list->DrawIndexed(
                        renderable->GetIndexCount(draw_call.lod_index),
                        renderable->GetIndexOffset(draw_call.lod_index),
                        renderable->GetVertexOffset(draw_call.lod_index)
                    );
                }
            }
    
            // create mip chain
            Pass_Blit(cmd_list, tex_occluders, tex_occluders_hiz);
            Pass_Downscale(cmd_list, tex_occluders_hiz, Renderer_DownsampleFilter::Min);
    
            // occlusion
            {
                // define pipeline state
                RHI_PipelineState pso;
                pso.name             = "occlusion";
                pso.shaders[Compute] = GetShader(Renderer_Shader::occlusion_c);
    
                cmd_list->SetPipelineState(pso);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_occluders_hiz);

                // set aabb count
                m_pcb_pass_cpu.set_f4_value(GetViewport().width, GetViewport().height, static_cast<float>(m_draw_call_count), static_cast<float>(tex_occluders_hiz->GetMipCount()));
                cmd_list->PushConstants(m_pcb_pass_cpu);

                cmd_list->SetBuffer(Renderer_BindingsUav::visibility, GetBuffer(Renderer_Buffer::Visibility));

                // dispatch: ceil(aabb_count / 256) thread groups
                uint32_t thread_group_count = (m_draw_call_count + 255) / 256; // ceiling division
                cmd_list->Dispatch(thread_group_count, 1, 1);
            }
        }
        cmd_list->EndTimeblock();

        // perform occlusion queries and wait for the results to be ready
        cmd_list->Submit(0, true);
        cmd_list->WaitForExecution(false);

        // update the draw calls with the visibility results so all subsequent passes can use them
        uint32_t* visibility_data = static_cast<uint32_t*>(GetBuffer(Renderer_Buffer::Visibility)->GetMappedData());
        for (uint32_t i = 0; i < m_draw_call_count; i++)
        {
            Renderer_DrawCall& draw_call = m_draw_calls[i];
            draw_call.renderable->SetVisible(visibility_data[i], draw_call.instance_group_index);
        }
    }

    void Renderer::Pass_Depth_Prepass(RHI_CommandList* cmd_list)
    {
        // acquire resources
        RHI_Texture* tex_depth        = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);        // render resolution - base depth
        RHI_Texture* tex_depth_output = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_output); // output resolution
   
        // deduce rasterizer state
        bool is_wireframe                     = GetOption<bool>(Renderer_Option::Wireframe);
        RHI_RasterizerState* rasterizer_state = GetRasterizerState(Renderer_RasterizerState::Solid);
        rasterizer_state                      = is_wireframe ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : rasterizer_state;

        cmd_list->BeginTimeblock("depth_prepass");
        {
            // set pipeline state
            RHI_PipelineState pso;
            pso.name                             = "depth_prepass";
            pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::depth_prepass_v);
            pso.rasterizer_state                 = rasterizer_state;
            pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
            pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
            pso.vrs_input_texture                = GetOption<bool>(Renderer_Option::VariableRateShading) ? GetRenderTarget(Renderer_RenderTarget::shading_rate) : nullptr;
            pso.render_target_depth_texture      = tex_depth;
            pso.resolution_scale                 = true;
            pso.clear_depth                      = 0.0f;

            for (uint32_t i = 0; i < m_draw_call_count; i++)
            {
                const Renderer_DrawCall& draw_call = m_draw_calls[i];
                Renderable* renderable             = draw_call.renderable;
                Material* material                 = renderable->GetMaterial();
                if (!material || material->IsTransparent() || !renderable->IsVisible(draw_call.instance_group_index))
                    continue;
    
                // toggles
                {
                    // alpha testing & tessellation
                    pso.shaders[RHI_Shader_Type::Pixel]  = material->IsAlphaTested()                                    ? GetShader(Renderer_Shader::depth_prepass_alpha_test_p) : nullptr;
                    pso.shaders[RHI_Shader_Type::Hull]   = material->GetProperty(MaterialProperty::Tessellation) > 0.0f ? GetShader(Renderer_Shader::tessellation_h)             : nullptr;
                    pso.shaders[RHI_Shader_Type::Domain] = material->GetProperty(MaterialProperty::Tessellation) > 0.0f ? GetShader(Renderer_Shader::tessellation_d)             : nullptr;

                     // culling
                    RHI_CullMode cull_mode = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode));
                    cull_mode              = (pso.rasterizer_state->GetPolygonMode() == RHI_PolygonMode::Wireframe) ? RHI_CullMode::None : cull_mode;
                    cmd_list->SetCullMode(cull_mode);
                    cmd_list->SetPipelineState(pso);
                }
    
                // set pass constants
                {
                    bool is_tessellated    = material->GetProperty(MaterialProperty::Tessellation) > 0.0f;
                    bool has_color_texture = material->HasTextureOfType(MaterialTextureType::Color);
                    m_pcb_pass_cpu.set_f3_value(is_tessellated ? 1.0f : 0.0f, has_color_texture ? 1.0f : 0.0f, static_cast<float>(i));
                    m_pcb_pass_cpu.set_is_transparent_and_material_index(false, material->GetIndex());
                    m_pcb_pass_cpu.transform = renderable->GetEntity()->GetMatrix();
                    cmd_list->PushConstants(m_pcb_pass_cpu);
                }

                // draw
                {
                    cmd_list->SetBufferVertex(renderable->GetVertexBuffer(), renderable->GetInstanceBuffer());
                    cmd_list->SetBufferIndex(renderable->GetIndexBuffer());

                    if (renderable->HasInstancing())
                    {
                        SP_ASSERT(draw_call.instance_count < renderable->GetInstanceBuffer()->GetElementCount());

                        cmd_list->DrawIndexed(
                            renderable->GetIndexCount(draw_call.lod_index),
                            renderable->GetIndexOffset(draw_call.lod_index),
                            renderable->GetVertexOffset(draw_call.lod_index),
                            draw_call.instance_index,
                            draw_call.instance_count
                        );
                    }
                    else
                    {
                        cmd_list->DrawIndexed(
                            renderable->GetIndexCount(draw_call.lod_index),
                            renderable->GetIndexOffset(draw_call.lod_index),
                            renderable->GetVertexOffset(draw_call.lod_index)
                        );
                    }

                    // at this point, we don't want clear in case another render pass is implicitly started
                    pso.clear_depth = rhi_depth_load;
                }
            }

            // blit to output resolution
            float resolution_scale = GetOption<float>(Renderer_Option::ResolutionScale);
            cmd_list->Blit(tex_depth, tex_depth_output, false, resolution_scale);
    
            // perform early resource transitions
            {
                tex_depth->SetLayout(RHI_Image_Layout::Attachment, cmd_list);
                tex_depth_output->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
                cmd_list->InsertPendingBarrierGroup();
            }
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_GBuffer(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        // acquire resources
        RHI_Texture* tex_color    = GetRenderTarget(Renderer_RenderTarget::gbuffer_color);
        RHI_Texture* tex_normal   = GetRenderTarget(Renderer_RenderTarget::gbuffer_normal);
        RHI_Texture* tex_material = GetRenderTarget(Renderer_RenderTarget::gbuffer_material);
        RHI_Texture* tex_velocity = GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity);
        RHI_Texture* tex_depth    = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
    
        cmd_list->BeginTimeblock(is_transparent_pass ? "g_buffer_transparent" : "g_buffer");
    
        // deduce rasterizer state
        bool is_wireframe                     = GetOption<bool>(Renderer_Option::Wireframe);
        RHI_RasterizerState* rasterizer_state = GetRasterizerState(Renderer_RasterizerState::Solid);
        rasterizer_state                      = is_wireframe ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : rasterizer_state;

        // deduce depth stencil state
        RHI_DepthStencilState* depth_stencil_state = is_transparent_pass ? GetDepthStencilState(Renderer_DepthStencilState::ReadWrite) : GetDepthStencilState(Renderer_DepthStencilState::ReadEqual);
    
        // set pipeline state
        RHI_PipelineState pso;
        pso.name                             = is_transparent_pass ? "g_buffer_transparent" : "g_buffer";
        pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::gbuffer_v);
        pso.shaders[RHI_Shader_Type::Pixel]  = GetShader(Renderer_Shader::gbuffer_p);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
        pso.rasterizer_state                 = rasterizer_state;
        pso.depth_stencil_state              = depth_stencil_state;
        pso.vrs_input_texture                = GetOption<bool>(Renderer_Option::VariableRateShading) ? GetRenderTarget(Renderer_RenderTarget::shading_rate) : nullptr;
        pso.resolution_scale                 = true;
        pso.render_target_color_textures[0]  = tex_color;
        pso.render_target_color_textures[1]  = tex_normal;
        pso.render_target_color_textures[2]  = tex_material;
        pso.render_target_color_textures[3]  = tex_velocity;
        pso.render_target_depth_texture      = tex_depth;
        pso.clear_color[0]                   = is_transparent_pass ? rhi_color_load : Color::standard_transparent;
        pso.clear_color[1]                   = is_transparent_pass ? rhi_color_load : Color::standard_transparent;
        pso.clear_color[2]                   = is_transparent_pass ? rhi_color_load : Color::standard_transparent;
        pso.clear_color[3]                   = is_transparent_pass ? rhi_color_load : Color::standard_transparent;
    
        bool set_pipeline = true;
        for (uint32_t i = 0; i < m_draw_call_count; i++)
        {
            const Renderer_DrawCall& draw_call = m_draw_calls[i];
            Renderable* renderable             = draw_call.renderable;
            Material* material                 = renderable->GetMaterial();
            if (!material || material->IsTransparent() != is_transparent_pass || !renderable->IsVisible(draw_call.instance_group_index))
                continue;
    
            // toggles
            {
                // tessellation & culling
                RHI_CullMode cull_mode = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode));
                cull_mode              = is_wireframe ? RHI_CullMode::None : cull_mode;
                cmd_list->SetCullMode(cull_mode);
    
                bool is_tessellated = material->GetProperty(MaterialProperty::Tessellation) > 0.0f;
                if ((is_tessellated && !pso.shaders[RHI_Shader_Type::Hull]) || (!is_tessellated && pso.shaders[RHI_Shader_Type::Hull]))
                {
                    pso.shaders[RHI_Shader_Type::Hull]   = is_tessellated ? GetShader(Renderer_Shader::tessellation_h) : nullptr;
                    pso.shaders[RHI_Shader_Type::Domain] = is_tessellated ? GetShader(Renderer_Shader::tessellation_d) : nullptr;
                    set_pipeline                         = true;
                }
    
                if (set_pipeline)
                {
                    cmd_list->SetPipelineState(pso);
                    set_pipeline = false;
                }
            }
    
            // set pass constants
            {
                Entity* entity           = renderable->GetEntity();
                m_pcb_pass_cpu.transform = entity->GetMatrix();
                m_pcb_pass_cpu.set_transform_previous(entity->GetMatrixPrevious());
                m_pcb_pass_cpu.set_is_transparent_and_material_index(is_transparent_pass, material->GetIndex());
                cmd_list->PushConstants(m_pcb_pass_cpu);
    
                entity->SetMatrixPrevious(m_pcb_pass_cpu.transform);
            }
    
            // draw
            {
                cmd_list->SetBufferVertex(renderable->GetVertexBuffer(), renderable->GetInstanceBuffer());
                cmd_list->SetBufferIndex(renderable->GetIndexBuffer());
    
                if (renderable->HasInstancing())
                {
                    cmd_list->DrawIndexed(
                        renderable->GetIndexCount(draw_call.lod_index),
                        renderable->GetIndexOffset(draw_call.lod_index),
                        renderable->GetVertexOffset(draw_call.lod_index),
                        draw_call.instance_index,
                        draw_call.instance_count
                    );
                }
                else
                {
                    cmd_list->DrawIndexed(
                        renderable->GetIndexCount(draw_call.lod_index),
                        renderable->GetIndexOffset(draw_call.lod_index),
                        renderable->GetVertexOffset(draw_call.lod_index)
                    );
                }

                // at this point, we don't want clear in case another render pass is implicitly started
                pso.clear_depth = rhi_depth_load;
            }
        }
    
        // perform early resource transitions
        tex_color->SetLayout(RHI_Image_Layout::General, cmd_list);
        tex_normal->SetLayout(RHI_Image_Layout::General, cmd_list);
        tex_material->SetLayout(RHI_Image_Layout::General, cmd_list);
        tex_velocity->SetLayout(RHI_Image_Layout::General, cmd_list);
        tex_depth->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list); // Pass_Sss() reads it as a srv
        cmd_list->InsertPendingBarrierGroup();
    
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Ssao(RHI_CommandList* cmd_list)
    {
        static bool cleared = false;
        RHI_Texture* tex_ssao = GetRenderTarget(Renderer_RenderTarget::ssao);

        if (GetOption<bool>(Renderer_Option::ScreenSpaceAmbientOcclusion))
        {
            RHI_PipelineState pso;
            pso.name             = "ssao";
            pso.shaders[Compute] = GetShader(Renderer_Shader::ssao_c);

            cmd_list->BeginTimeblock(pso.name);
            {
                cmd_list->SetPipelineState(pso);
                SetCommonTextures(cmd_list);
                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_ssao);
                cmd_list->Dispatch(tex_ssao);

                cleared = false;
            }
            cmd_list->EndTimeblock();
        }
        else if (!cleared)
        {
            cmd_list->ClearTexture(tex_ssao, Color::standard_white);
            cleared = true;
        }
    }

    void Renderer::Pass_Ssr(RHI_CommandList* cmd_list)
    {
        static bool cleared = false;

        RHI_Texture* tex_out = GetRenderTarget(Renderer_RenderTarget::ssr);

        if (GetOption<bool>(Renderer_Option::ScreenSpaceReflections))
        { 
            cmd_list->BeginTimeblock("ssr");
            {
                // do any pending barriers as we don't have control over fidelityfx sssr
                GetRenderTarget(Renderer_RenderTarget::source_refraction)->SetLayout(RHI_Image_Layout::General, cmd_list);
                cmd_list->InsertPendingBarrierGroup();
                cmd_list->RenderPassEnd();

                RHI_VendorTechnology::SSSR_Dispatch(
                    cmd_list,
                    GetOption<float>(Renderer_Option::ResolutionScale),
                    GetRenderTarget(Renderer_RenderTarget::source_refraction),
                    GetRenderTarget(Renderer_RenderTarget::gbuffer_depth),
                    GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity),
                    GetRenderTarget(Renderer_RenderTarget::gbuffer_normal),
                    GetRenderTarget(Renderer_RenderTarget::gbuffer_material),
                    GetRenderTarget(Renderer_RenderTarget::brdf_specular_lut),
                    tex_out
                );

                cleared = false;
            }
            cmd_list->EndTimeblock();
        }
        else if (!cleared)
        {
            cmd_list->ClearTexture(tex_out, Color::standard_transparent);
            cleared = true;
        }

        // wait for vendor tech to finish writing to the texture
        cmd_list->InsertBarrierReadWrite(tex_out);
    }

    void Renderer::Pass_Sss(RHI_CommandList* cmd_list)
    {
        // get resources
        RHI_Texture* tex_sss= GetRenderTarget(Renderer_RenderTarget::sss);

        cmd_list->BeginTimeblock("sss");
        {
            // set pipeline state
            RHI_PipelineState pso;
            pso.name             = "sss";
            pso.shaders[Compute] = GetShader(Renderer_Shader::sss_c_bend);
            cmd_list->SetPipelineState(pso);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,     GetRenderTarget(Renderer_RenderTarget::gbuffer_depth)); // read
            cmd_list->SetTexture(Renderer_BindingsUav::tex_sss, tex_sss);                                               // write

            // iterate through all the lights
            static float array_slice_index = 0.0f;
            for (const shared_ptr<Entity>& entity : World::GetEntities())
            {
                if (Light* light = entity->GetComponent<Light>())
                {
                    if (!light->GetFlag(LightFlags::ShadowsScreenSpace) || light->GetIntensityWatt() == 0.0f)
                        continue;

                    if (array_slice_index == tex_sss->GetDepth())
                    {
                        SP_LOG_WARNING("Render target has reached the maximum number of lights it can hold");
                        break;
                    }

                    math::Matrix view_projection = World::GetCamera()->GetViewProjectionMatrix();
                    Vector4 p = {};
                    if (light->GetLightType() == LightType::Directional)
                    {
                        // todo: Why do we need to flip sign?
                        p = Vector4(-light->GetEntity()->GetForward(), 0.0f) * view_projection;
                    }
                    else
                    {
                        p = Vector4(light->GetEntity()->GetPosition(), 1.0f) * view_projection;
                    }

                    float in_light_projection[]      = { p.x, p.y, p.z, p.w };
                    int32_t in_viewport_size[]       = { static_cast<int32_t>(tex_sss->GetWidth()), static_cast<int32_t>(tex_sss->GetHeight()) };
                    int32_t in_min_render_bounds[]   = { 0, 0 };
                    int32_t in_max_render_bounds[]   = { static_cast<int32_t>(tex_sss->GetWidth()), static_cast<int32_t>(tex_sss->GetHeight()) };
                    Bend::DispatchList dispatch_list = Bend::BuildDispatchList(in_light_projection, in_viewport_size, in_min_render_bounds, in_max_render_bounds, false);

                    m_pcb_pass_cpu.set_f4_value
                    (
                        dispatch_list.LightCoordinate_Shader[0],
                        dispatch_list.LightCoordinate_Shader[1],
                        dispatch_list.LightCoordinate_Shader[2],
                        dispatch_list.LightCoordinate_Shader[3]
                    );

                    // light index writes into the texture array index
                    float near = 1.0f;
                    float far  = 0.0f;
                    m_pcb_pass_cpu.set_f3_value(near, far, array_slice_index++);
                    m_pcb_pass_cpu.set_f3_value2(1.0f / tex_sss->GetWidth(), 1.0f / tex_sss->GetHeight(), 0.0f);

                    for (int32_t dispatch_index = 0; dispatch_index < dispatch_list.DispatchCount; ++dispatch_index)
                    {
                        const Bend::DispatchData& dispatch = dispatch_list.Dispatch[dispatch_index];
                        m_pcb_pass_cpu.set_f2_value(static_cast<float>(dispatch.WaveOffset_Shader[0]), static_cast<float>(dispatch.WaveOffset_Shader[1]));
                        cmd_list->PushConstants(m_pcb_pass_cpu);
                        cmd_list->Dispatch(dispatch.WaveCount[0], dispatch.WaveCount[1], dispatch.WaveCount[2]);
                    }

                    cmd_list->InsertBarrierReadWrite(tex_sss); // ensure the texture is ready for the next light
                }
            }

            array_slice_index = 0;
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Skysphere(RHI_CommandList* cmd_list)
    {
        Light* light = World::GetDirectionalLight();
        if (!light)
            return;
    
        RHI_Texture* tex_environment = GetRenderTarget(Renderer_RenderTarget::skysphere);

        cmd_list->BeginTimeblock("skysphere");
        {
            // 1. atmospheric scattering
            {
                RHI_PipelineState pso;
                pso.name             = "skysphere_atmospheric_scattering";
                pso.shaders[Compute] = GetShader(Renderer_Shader::skysphere_c);
                cmd_list->SetPipelineState(pso);
    
                m_pcb_pass_cpu.set_f3_value2(static_cast<float>(light->GetIndex()), 0.0f, 0.0f);
                cmd_list->PushConstants(m_pcb_pass_cpu);
    
                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_environment);
                cmd_list->Dispatch(tex_environment);
            }

            // 2. filter all mip levels (top half only, uv.y <= 0.5) to cover the visible upper hemisphere
            // (zenith to horizon). The bottom half is below the horizon, invisible, and mostly black in the
            // atmospheric scattering shader, so we skip it to reduce dispatch costs
            {
                // filtering can sample from any mip, so we need to generate the mip chain
                Pass_Downscale(cmd_list, tex_environment, Renderer_DownsampleFilter::Average);

                RHI_PipelineState pso;
                pso.name             = "skysphere_filter";
                pso.shaders[Compute] = GetShader(Renderer_Shader::light_integration_environment_filter_c);
                cmd_list->SetPipelineState(pso);
            
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_environment);
            
                for (uint32_t mip_level = 1; mip_level < tex_environment->GetMipCount(); mip_level++)
                {
                    cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_environment, mip_level, 1);
            
                    // Set pass constants
                    m_pcb_pass_cpu.set_f3_value(static_cast<float>(mip_level), static_cast<float>(tex_environment->GetMipCount()), 0.0f);
                    cmd_list->PushConstants(m_pcb_pass_cpu);
            
                    const uint32_t resolution_x = tex_environment->GetWidth() >> mip_level;
                    const uint32_t resolution_y = tex_environment->GetHeight() >> mip_level;
                    cmd_list->Dispatch(
                        static_cast<uint32_t>(ceil(static_cast<float>(resolution_x) / 8)),
                        static_cast<uint32_t>(ceil(static_cast<float>(resolution_y / 2.0f) / 8)) // dispatch top half only
                    );
                    cmd_list->InsertBarrierReadWrite(tex_environment);
                }
            }
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Light(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        // early exit if no entities or no camera
        if (World::GetLightCount() == 0 || !World::GetCamera())
            return;
    
        cmd_list->BeginTimeblock(is_transparent_pass ? "light_transparent" : "light");
        {
            // set pipeline state
            RHI_PipelineState pso;
            pso.name             = is_transparent_pass ? "light_transparent" : "light";
            pso.shaders[Compute] = GetShader(Renderer_Shader::light_c);
            cmd_list->SetPipelineState(pso);
    
            // set static textures once
            cmd_list->SetTexture(Renderer_BindingsUav::tex_sss, GetRenderTarget(Renderer_RenderTarget::sss));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,     GetRenderTarget(Renderer_RenderTarget::skysphere));
    
            // track light index for clearing (first light clears render targets)
            uint32_t light_index = 0;
    
            // process all lights
            for (const shared_ptr<Entity>& entity : World::GetEntitiesLights())
            {
                Light* light = entity->GetComponent<Light>();

                RHI_Texture* light_diffuse    = GetRenderTarget(Renderer_RenderTarget::light_diffuse);
                RHI_Texture* light_specular   = GetRenderTarget(Renderer_RenderTarget::light_specular);
                RHI_Texture* light_shadow     = GetRenderTarget(Renderer_RenderTarget::light_shadow);
                RHI_Texture* light_volumetric = GetRenderTarget(Renderer_RenderTarget::light_volumetric);

                // set textures
                SetCommonTextures(cmd_list);
                cmd_list->SetTexture(Renderer_BindingsSrv::light_depth, light->GetDepthTexture());
                cmd_list->SetTexture(Renderer_BindingsUav::tex,         light_diffuse);
                cmd_list->SetTexture(Renderer_BindingsUav::tex2,        light_specular);
                cmd_list->SetTexture(Renderer_BindingsUav::tex3,        light_shadow);
                cmd_list->SetTexture(Renderer_BindingsUav::tex4,        light_volumetric);

                // push pass constants
                m_pcb_pass_cpu.set_is_transparent_and_material_index(is_transparent_pass);
                m_pcb_pass_cpu.set_f3_value2(static_cast<float>(light_index++), 0.0f, 0.0f);
                m_pcb_pass_cpu.set_f3_value(
                    GetOption<float>(Renderer_Option::Fog),
                    GetOption<float>(Renderer_Option::ShadowResolution),
                    static_cast<float>(GetRenderTarget(Renderer_RenderTarget::skysphere)->GetMipCount())
                );
                cmd_list->PushConstants(m_pcb_pass_cpu);
    
                // dispatch
                cmd_list->Dispatch(light_diffuse); // includes InsertBarrierReadWrite(light_diffuse)

                // the above dispatch will add a read/write barrier to the diffuse texture
                cmd_list->InsertBarrierReadWrite(light_specular);
                cmd_list->InsertBarrierReadWrite(light_shadow);
                cmd_list->InsertBarrierReadWrite(light_volumetric);
            }
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Light_GlobalIllumination(RHI_CommandList* cmd_list)
    {
        static bool cleared = true;

        if (GetOption<float>(Renderer_Option::GlobalIllumination) != 0.0f)
        { 
            cmd_list->BeginTimeblock("light_global_illumination");

            // update
            {
                RHI_VendorTechnology::BrixelizerGI_Update(
                    cmd_list,
                    GetOption<float>(Renderer_Option::ResolutionScale),
                    &m_cb_frame_cpu,
                    World::GetEntities(),
                    GetRenderTarget(Renderer_RenderTarget::light_diffuse_gi) // use as debug output (if needed)
                );
            }

            // dispatch
            {
                static array<RHI_Texture*, 8> noise_textures =
                {
                    GetStandardTexture(Renderer_StandardTexture::Noise_blue_0),
                    GetStandardTexture(Renderer_StandardTexture::Noise_blue_1),
                    GetStandardTexture(Renderer_StandardTexture::Noise_blue_2),
                    GetStandardTexture(Renderer_StandardTexture::Noise_blue_3),
                    GetStandardTexture(Renderer_StandardTexture::Noise_blue_4),
                    GetStandardTexture(Renderer_StandardTexture::Noise_blue_5),
                    GetStandardTexture(Renderer_StandardTexture::Noise_blue_6),
                    GetStandardTexture(Renderer_StandardTexture::Noise_blue_7)
                };

                RHI_VendorTechnology::BrixelizerGI_Dispatch(
                    cmd_list,
                    &m_cb_frame_cpu,
                    GetRenderTarget(Renderer_RenderTarget::source_gi),
                    GetRenderTarget(Renderer_RenderTarget::gbuffer_depth),
                    GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity),
                    GetRenderTarget(Renderer_RenderTarget::gbuffer_normal),
                    GetRenderTarget(Renderer_RenderTarget::gbuffer_material),
                    noise_textures,
                    GetRenderTarget(Renderer_RenderTarget::light_diffuse_gi),
                    GetRenderTarget(Renderer_RenderTarget::light_specular_gi),
                    GetRenderTarget(Renderer_RenderTarget::light_diffuse_gi) // use as debug output (if needed)
                );
            }

            cleared = false;

            cmd_list->EndTimeblock();
        }
        else if (!cleared)
        {
            cmd_list->ClearTexture(GetRenderTarget(Renderer_RenderTarget::light_diffuse_gi),  Color::standard_black);
            cmd_list->ClearTexture(GetRenderTarget(Renderer_RenderTarget::light_specular_gi), Color::standard_black);
            cleared = true;
        }
    }

    void Renderer::Pass_Light_Composition(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        // acquire resources
        RHI_Shader* shader_c              = GetShader(Renderer_Shader::light_composition_c);
        RHI_Texture* tex_out              = GetRenderTarget(Renderer_RenderTarget::frame_render);
        RHI_Texture* tex_skysphere        = GetRenderTarget(Renderer_RenderTarget::skysphere);
        RHI_Texture* tex_refraction       = GetRenderTarget(Renderer_RenderTarget::source_refraction);
        RHI_Texture* tex_light_diffuse    = GetRenderTarget(Renderer_RenderTarget::light_diffuse);
        RHI_Texture* tex_light_specular   = GetRenderTarget(Renderer_RenderTarget::light_specular);
        RHI_Texture* tex_light_volumetric = GetRenderTarget(Renderer_RenderTarget::light_volumetric);

        cmd_list->BeginTimeblock(is_transparent_pass ? "light_composition_transparent" : "light_composition");
        {
            // set pipeline state
            RHI_PipelineState pso;
            pso.name             = "light_composition";
            pso.shaders[Compute] = shader_c;
            cmd_list->SetPipelineState(pso);

            // push pass constants
            m_pcb_pass_cpu.set_is_transparent_and_material_index(is_transparent_pass);
            m_pcb_pass_cpu.set_f3_value(static_cast<float>(tex_skysphere->GetMipCount()), GetOption<float>(Renderer_Option::Fog), 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            // set textures
            SetCommonTextures(cmd_list);
            cmd_list->SetTexture(Renderer_BindingsUav::tex,  tex_out);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,  GetStandardTexture(Renderer_StandardTexture::Foam));
            cmd_list->SetTexture(Renderer_BindingsUav::tex2, tex_light_diffuse);
            cmd_list->SetTexture(Renderer_BindingsUav::tex3, tex_light_specular);
            cmd_list->SetTexture(Renderer_BindingsUav::tex4, tex_light_volumetric);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_refraction);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_skysphere);

            // render
            cmd_list->Dispatch(tex_out);

            // create sampling source for refraction
            cmd_list->Blit(tex_out, tex_refraction, false);
            Pass_Downscale(cmd_list, tex_refraction, Renderer_DownsampleFilter::Average); // emulate roughness for refraction

            cmd_list->InsertBarrierReadWrite(tex_light_diffuse);
            cmd_list->InsertBarrierReadWrite(tex_light_specular);
            cmd_list->InsertBarrierReadWrite(tex_light_volumetric);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Light_ImageBased(RHI_CommandList* cmd_list)
    {
        // acquire resources
        RHI_Shader* shader   = GetShader(Renderer_Shader::light_image_based_c);
        RHI_Texture* tex_out = GetRenderTarget(Renderer_RenderTarget::frame_render);

        cmd_list->BeginTimeblock("light_image_based");
        {
            // set pipeline state
            RHI_PipelineState pso;
            pso.name             = "light_image_based";
            pso.shaders[Compute] = shader;
            cmd_list->SetPipelineState(pso);

            // set textures
            SetCommonTextures(cmd_list);
            cmd_list->SetTexture(Renderer_BindingsUav::tex3,    GetRenderTarget(Renderer_RenderTarget::light_diffuse_gi));
            cmd_list->SetTexture(Renderer_BindingsUav::tex4,    GetRenderTarget(Renderer_RenderTarget::light_specular_gi));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2,    GetRenderTarget(Renderer_RenderTarget::ssr));
            cmd_list->SetTexture(Renderer_BindingsUav::tex_sss, GetRenderTarget(Renderer_RenderTarget::sss));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3,    GetRenderTarget(Renderer_RenderTarget::brdf_specular_lut));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex4,    GetRenderTarget(Renderer_RenderTarget::skysphere));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,     GetRenderTarget(Renderer_RenderTarget::light_shadow));
            cmd_list->SetTexture(Renderer_BindingsUav::tex,     tex_out);

            // set pass constants
            m_pcb_pass_cpu.set_f3_value(static_cast<float>(GetRenderTarget(Renderer_RenderTarget::skysphere)->GetMipCount()));
            cmd_list->PushConstants(m_pcb_pass_cpu);

            // render
            cmd_list->Dispatch(tex_out);

            // create sampling source for gi
            cmd_list->Blit(tex_out, GetRenderTarget(Renderer_RenderTarget::source_gi), false);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Light_Integration_BrdfSpecularLut(RHI_CommandList* cmd_list)
    {
        cmd_list->BeginTimeblock("light_integration_brdf_specular_lut");
        {
            // set pipeline state
            RHI_PipelineState pso;
            pso.name             = "light_integration_brdf_specular_lut";
            pso.shaders[Compute] = GetShader(Renderer_Shader::light_integration_brdf_specular_lut_c);
            cmd_list->SetPipelineState(pso);

            RHI_Texture* tex_brdf_specular_lut = GetRenderTarget(Renderer_RenderTarget::brdf_specular_lut);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_brdf_specular_lut);
            cmd_list->Dispatch(tex_brdf_specular_lut);

            // light pass and ssr pass both read it as an srv, so transition once here
            cmd_list->InsertBarrier(tex_brdf_specular_lut->GetRhiResource(), tex_brdf_specular_lut->GetFormat(), 0, 1, 1, RHI_Image_Layout::Shader_Read);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_PostProcess(RHI_CommandList* cmd_list)
    {
        // acquire render targets
        RHI_Texture* rt_frame_output         = GetRenderTarget(Renderer_RenderTarget::frame_output);
        RHI_Texture* rt_frame_output_scratch = GetRenderTarget(Renderer_RenderTarget::frame_output_2);

        cmd_list->BeginMarker("post_proccess");

        // macros which allows us to keep track of which texture is an input/output for each pass
        bool swap_output = true;
        #define get_output_in  swap_output ? rt_frame_output_scratch : rt_frame_output
        #define get_output_out swap_output ? rt_frame_output : rt_frame_output_scratch

        // depth of field
        if (GetOption<bool>(Renderer_Option::DepthOfField))
        {
            swap_output = !swap_output;
            Pass_DepthOfField(cmd_list, get_output_in, get_output_out);
        }
        
        // motion blur
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
        {
            swap_output = !swap_output;
            Pass_Output(cmd_list, get_output_in, get_output_out);
        }

        // dithering
        if (GetOption<bool>(Renderer_Option::Dithering))
        {
            swap_output = !swap_output;
            Pass_Dithering(cmd_list, get_output_in, get_output_out);
        }

        // sharpening
        if (GetOption<bool>(Renderer_Option::Sharpness) && GetOption<Renderer_Upsampling>(Renderer_Option::Upsampling) != Renderer_Upsampling::Fsr3)
        {
            swap_output = !swap_output;
            Pass_Sharpening(cmd_list, get_output_in, get_output_out);
        }
        
        // fxaa
        Renderer_Antialiasing antialiasing  = GetOption<Renderer_Antialiasing>(Renderer_Option::Antialiasing);
        bool fxaa_enabled                   = antialiasing == Renderer_Antialiasing::Fxaa || antialiasing == Renderer_Antialiasing::TaaFxaa;
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

        // if the last written texture is not the output one, then make sure it is
        if (!swap_output)
        {
            cmd_list->Copy(rt_frame_output_scratch, rt_frame_output, false);
        }

        cmd_list->EndMarker();
    }

    void Renderer::Pass_Bloom(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // acquire resources
        RHI_Shader* shader_luminance          = GetShader(Renderer_Shader::bloom_luminance_c);
        RHI_Shader* shader_upsample_blend_mip = GetShader(Renderer_Shader::bloom_upsample_blend_mip_c);
        RHI_Shader* shader_blend_frame        = GetShader(Renderer_Shader::bloom_blend_frame_c);
        RHI_Texture* tex_bloom                = GetRenderTarget(Renderer_RenderTarget::bloom);

        cmd_list->BeginTimeblock("bloom");

        // luminance
        cmd_list->BeginMarker("luminance");
        {
            // define pipeline state
            RHI_PipelineState pso;
            pso.name             = "bloom_luminance";
            pso.shaders[Compute] = shader_luminance;

            // set pipeline state
            cmd_list->SetPipelineState(pso);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_bloom);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

            // render
            cmd_list->Dispatch(tex_bloom);
        }
        cmd_list->EndMarker();

        // generate mips
        Pass_Downscale(cmd_list, tex_bloom, Renderer_DownsampleFilter::Average);

        // starting from the lowest mip, upsample and blend with the higher one
        cmd_list->BeginMarker("upsample_and_blend_with_higher_mip");
        {
            // define pipeline state
            RHI_PipelineState pso;
            pso.name             = "bloom_upsample_blend_mip";
            pso.shaders[Compute] = shader_upsample_blend_mip;

            // set pipeline state
            cmd_list->SetPipelineState(pso);

            // render
            for (int i = static_cast<int>(tex_bloom->GetMipCount() - 1); i > 0; i--)
            {
                int mip_index_small   = i;
                int mip_index_big     = i - 1;
                int mip_width_large   = tex_bloom->GetWidth() >> mip_index_big;
                int mip_height_height = tex_bloom->GetHeight() >> mip_index_big;

                // set textures
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_bloom, mip_index_small, 1);
                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_bloom, mip_index_big, 1);

                // Blend
                uint32_t thread_group_count    = 8;
                uint32_t thread_group_count_x_ = static_cast<uint32_t>(ceil(static_cast<float>(mip_width_large) / thread_group_count));
                uint32_t thread_group_count_y_ = static_cast<uint32_t>(ceil(static_cast<float>(mip_height_height) / thread_group_count));
                cmd_list->Dispatch(thread_group_count_x_, thread_group_count_y_);
            }
        }
        cmd_list->EndMarker();

        // blend with the frame
        cmd_list->BeginMarker("blend_with_frame");
        {
            // define pipeline state
            RHI_PipelineState pso;
            pso.name             = "bloom_blend_frame";
            pso.shaders[Compute] = shader_blend_frame;

            // set pipeline state
            cmd_list->SetPipelineState(pso);

            // set pass constants
            m_pcb_pass_cpu.set_f3_value(GetOption<float>(Renderer_Option::Bloom), 0.0f, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_bloom, 0, 1);

            // render
            cmd_list->Dispatch(tex_out);
        }
        cmd_list->EndMarker();

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Output(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // acquire shaders
        RHI_Shader* shader_c = GetShader(Renderer_Shader::output_c);
 
        cmd_list->BeginTimeblock("output");

        // set pipeline state
        RHI_PipelineState pso;
        pso.name             = "output";
        pso.shaders[Compute] = shader_c;
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_pcb_pass_cpu.set_f3_value(GetOption<float>(Renderer_Option::Tonemapping));
        cmd_list->PushConstants(m_pcb_pass_cpu);

        // set textures
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

        // render
        cmd_list->Dispatch(tex_out);

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Fxaa(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        cmd_list->BeginTimeblock("fxaa");

        // set pipeline state
        RHI_PipelineState pso;
        pso.name             = "fxaa";
        pso.shaders[Compute] = GetShader(Renderer_Shader::fxaa_c);
        cmd_list->SetPipelineState(pso);

        // set textures
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        
        // render
        cmd_list->Dispatch(tex_out);

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_ChromaticAberration(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        cmd_list->BeginTimeblock("chromatic_aberration");

        // define pipeline state
        RHI_PipelineState pso;
        pso.name             = "chromatic_aberration";
        pso.shaders[Compute] = GetShader(Renderer_Shader::chromatic_aberration_c);

        // set pipeline state
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_pcb_pass_cpu.set_f3_value(World::GetCamera()->GetAperture(), 0.0f, 0.0f);
        cmd_list->PushConstants(m_pcb_pass_cpu);

        // set textures
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

        // render
        cmd_list->Dispatch(tex_out);

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_MotionBlur(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // acquire shaders
        RHI_Shader* shader_c = GetShader(Renderer_Shader::motion_blur_c);

        cmd_list->BeginTimeblock("motion_blur");

        // define pipeline state
        RHI_PipelineState pso;
        pso.name             = "motion_blur";
        pso.shaders[Compute] = shader_c;

        // set pipeline state
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_pcb_pass_cpu.set_f3_value(World::GetCamera()->GetShutterSpeed(), 0.0f, 0.0f);
        cmd_list->PushConstants(m_pcb_pass_cpu);

        // set textures
        SetCommonTextures(cmd_list);
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

        // render
        cmd_list->Dispatch(tex_out);

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_DepthOfField(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // acquire shader
        RHI_Shader* shader_c = GetShader(Renderer_Shader::depth_of_field_c);
    
        cmd_list->BeginTimeblock("depth_of_field");

        // set pipeline state
        RHI_PipelineState pso;
        pso.name             = "depth_of_field";
        pso.shaders[Compute] = shader_c;
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_pcb_pass_cpu.set_f3_value(World::GetCamera()->GetAperture(), 0.0f, 0.0f);
        cmd_list->PushConstants(m_pcb_pass_cpu);

        // set textures
        SetCommonTextures(cmd_list);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        

        // render
        cmd_list->Dispatch(tex_out);

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_FilmGrain(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // acquire shader
        RHI_Shader* shader_c = GetShader(Renderer_Shader::film_grain_c);

        cmd_list->BeginTimeblock("film_grain");

        // set pipeline state
        RHI_PipelineState pso;
        pso.name             = "film_grain";
        pso.shaders[Compute] = shader_c;
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_pcb_pass_cpu.set_f3_value(World::GetCamera()->GetIso(), 0.0f, 0.0f);
        cmd_list->PushConstants(m_pcb_pass_cpu);

        // set textures
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

        // render
        cmd_list->Dispatch(tex_out);

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Upscale(RHI_CommandList* cmd_list)
    {
        // acquire render targets
        RHI_Texture* tex_in  = GetRenderTarget(Renderer_RenderTarget::frame_render);
        RHI_Texture* tex_out = GetRenderTarget(Renderer_RenderTarget::frame_output);

        cmd_list->BeginTimeblock("upscale");
        {
            if (GetOption<Renderer_Upsampling>(Renderer_Option::Upsampling) == Renderer_Upsampling::Fsr3)
            {
                RHI_VendorTechnology::FSR3_Dispatch(
                    cmd_list,
                    World::GetCamera(),
                    m_cb_frame_cpu.delta_time,
                    GetOption<float>(Renderer_Option::Sharpness),
                    GetOption<float>(Renderer_Option::ResolutionScale),
                    tex_in,
                    GetRenderTarget(Renderer_RenderTarget::gbuffer_depth),
                    GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity),
                    tex_out
                );
            }
            else if (GetOption<Renderer_Upsampling>(Renderer_Option::Upsampling) == Renderer_Upsampling::XeSS)
            {
                 RHI_VendorTechnology::XeSS_Dispatch(
                    cmd_list,
                    GetOption<float>(Renderer_Option::ResolutionScale),
                    tex_in,
                    GetRenderTarget(Renderer_RenderTarget::gbuffer_depth),
                    GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity),
                    tex_out
                );
            }
            else // no upscale or linear upscale
            {
                cmd_list->Blit(tex_in, tex_out, false, GetOption<float>(Renderer_Option::ResolutionScale));
            }

            // wait for vendor tech to finish writing to the texture
            cmd_list->InsertBarrierReadWrite(tex_in);
            cmd_list->InsertBarrierReadWrite(tex_out);

            // used for refraction by the transparent passes, so generate mips to emulate roughness
            Pass_Downscale(cmd_list, tex_out, Renderer_DownsampleFilter::Average);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Blit(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // we use a compute shader to blit from depth to float, as Vulkan doesn't support blitting depth to float formats
        // and amd hardware requires UAV textures to be float-based (preventing depth format usage)
        // if the above is not your case, use RHI_CommandList::Blit instead, which is the fastest option

        // acquire resources
        RHI_Shader* shader_c = GetShader(Renderer_Shader::blit_c);

        cmd_list->BeginTimeblock("blit");
        {
            // set pipeline state
            RHI_PipelineState pso;
            pso.name             = "blit";
            pso.shaders[Compute] = shader_c;
            cmd_list->SetPipelineState(pso);
            
            // set textures
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            
            // render
            cmd_list->Dispatch(tex_out);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Downscale(RHI_CommandList* cmd_list, RHI_Texture* tex, const Renderer_DownsampleFilter filter)
    {
        // AMD FidelityFX Single Pass Downsampler.
        // Provides an RDNA™-optimized solution for generating up to 12 MIP levels of a texture.
        // GitHub:        https://github.com/GPUOpen-Effects/FidelityFX-SPD
        // Documentation: https://github.com/GPUOpen-Effects/FidelityFX-SPD/blob/master/docs/FidelityFX_SPD.pdf

        // deduce information
        const uint32_t mip_start             = 0;
        const uint32_t output_mip_count      = tex->GetMipCount() - (mip_start + 1);
        const uint32_t width                 = tex->GetWidth();
        const uint32_t height                = tex->GetHeight() >> mip_start;
        const uint32_t thread_group_count_x_ = (width + 63)  >> 6; // as per document documentation (page 22)
        const uint32_t thread_group_count_y_ = (height + 63) >> 6; // as per document documentation (page 22)

        // ensure that the input texture meets the requirements
        SP_ASSERT(tex->HasPerMipViews());
        SP_ASSERT(width <= 4096 && height <= 4096 && output_mip_count <= 12); // as per documentation (page 22)
        SP_ASSERT(mip_start < output_mip_count);

        // acquire shader
        Renderer_Shader shader = Renderer_Shader::ffx_spd_average_c;
        shader                 = filter == Renderer_DownsampleFilter::Min ? Renderer_Shader::ffx_spd_min_c : shader;
        shader                 = filter == Renderer_DownsampleFilter::Max ? Renderer_Shader::ffx_spd_max_c : shader;
        RHI_Shader* shader_c   = GetShader(shader);

        cmd_list->BeginMarker("downscale");
        {
            cmd_list->InsertBarrierReadWrite(GetBuffer(Renderer_Buffer::SpdCounter));

            // set pipeline state
            RHI_PipelineState pso;
            pso.name             = "downscale";
            pso.shaders[Compute] = shader_c;
            cmd_list->SetPipelineState(pso);

            // push pass data
            m_pcb_pass_cpu.set_f3_value(static_cast<float>(output_mip_count), static_cast<float>(thread_group_count_x_ * thread_group_count_y_), 0.0f);
            m_pcb_pass_cpu.set_f3_value2(static_cast<float>(tex->GetWidth()), static_cast<float>(tex->GetHeight()), 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            // set resources
            cmd_list->SetBuffer(Renderer_BindingsUav::sb_spd,   GetBuffer(Renderer_Buffer::SpdCounter)); // atomic counter
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,     tex, mip_start, 1);                      // starting mip
            cmd_list->SetTexture(Renderer_BindingsUav::tex_spd, tex, mip_start + 1, output_mip_count);   // following mips

            // render
            cmd_list->Dispatch(thread_group_count_x_, thread_group_count_y_);
            cmd_list->InsertBarrierReadWrite(tex);
        }
        cmd_list->EndMarker();
    }

    void Renderer::Pass_Sharpening(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        cmd_list->BeginTimeblock("sharpening");
        {
            // set pipeline state
            RHI_PipelineState pso;
            pso.name             = "sharpening";
            pso.shaders[Compute] = GetShader(Renderer_Shader::ffx_cas_c);
            cmd_list->SetPipelineState(pso);
            
            // set pass constants
            m_pcb_pass_cpu.set_f3_value(GetOption<float>(Renderer_Option::Sharpness), 0.0f, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);
            
            // set textures
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            
            // render
            cmd_list->Dispatch(tex_out);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Dithering(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        cmd_list->BeginTimeblock("dithering");
        {
            // set pipeline state
            RHI_PipelineState pso;
            pso.name             = "dithering";
            pso.shaders[Compute] = GetShader(Renderer_Shader::dithering_c);
            cmd_list->SetPipelineState(pso);
            
            // set textures
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, GetStandardTexture(Renderer_StandardTexture::Noise_blue_0));
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);

            // render
            cmd_list->Dispatch(tex_out);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Blur(RHI_CommandList* cmd_list, RHI_Texture* tex_in, const float radius, const uint32_t mip /*= rhi_all_mips*/)
    {
        // acquire shader
        RHI_Shader* shader_c = GetShader(Renderer_Shader::blur_gaussian_c);

        // compute thread group count
        const bool mip_requested            = mip != rhi_all_mips;
        const uint32_t mip_range            = mip_requested ? 1 : 0;
        const uint32_t bit_shift            = mip_requested ? mip : 0;
        const uint32_t width                = tex_in->GetWidth()  >> bit_shift;
        const uint32_t height               = tex_in->GetHeight() >> bit_shift;
        const uint32_t thread_group_count   = 8;
        const uint32_t thread_group_count_x = (width + thread_group_count - 1) / thread_group_count;
        const uint32_t thread_group_count_y = (height + thread_group_count - 1) / thread_group_count;

        // acquire blur scratch buffer
        RHI_Texture* tex_blur = GetRenderTarget(Renderer_RenderTarget::blur);
        SP_ASSERT_MSG(width <= tex_blur->GetWidth() && height <= tex_blur->GetHeight(), "Input texture is larger than the blur scratch buffer");

        cmd_list->BeginMarker("blur");

        // set pipeline state
        RHI_PipelineState pso;
        pso.name             = "blur";
        pso.shaders[Compute] = shader_c;
        cmd_list->SetPipelineState(pso);

        // horizontal pass
        {
            // set pass constants
            m_pcb_pass_cpu.set_f3_value(radius, 0.0f); // horizontal
            cmd_list->PushConstants(m_pcb_pass_cpu);

            // set textures
            SetCommonTextures(cmd_list);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in, mip, mip_range);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_blur); // write

            // render
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y);
        }

        // vertical pass
        {
            // set pass constants
            m_pcb_pass_cpu.set_f3_value(radius, 1.0f); // vertical
            cmd_list->PushConstants(m_pcb_pass_cpu);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_blur);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_in, mip, mip_range); // write

            // render
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y);
        }

        cmd_list->EndMarker();
    }

    void Renderer::Pass_Icons(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        // append icons from entities
        if (!Engine::IsFlagSet(EngineMode::Playing))
        { 
            for (const shared_ptr<Entity>& entity : World::GetEntities())
            {
                math::Vector3 pos_world = entity->GetPosition();
                if (entity->GetComponent<AudioSource>())
                {
                    m_icons.emplace_back(make_tuple(GetStandardTexture(Renderer_StandardTexture::Gizmo_audio_source), entity->GetPosition()));
                }
                else if (Light* light = entity->GetComponent<Light>())
                {
                    if (GetOption<bool>(Renderer_Option::Lights))
                    {
                        // append light icon based on type
                        RHI_Texture* texture = nullptr;
                        if (light->GetLightType() == LightType::Directional)
                            texture = GetStandardTexture(Renderer_StandardTexture::Gizmo_light_directional);
                        else if (light->GetLightType() == LightType::Point)
                            texture = GetStandardTexture(Renderer_StandardTexture::Gizmo_light_point);
                        else if (light->GetLightType() == LightType::Spot)
                            texture = GetStandardTexture(Renderer_StandardTexture::Gizmo_light_spot);

                        if (texture)
                        {
                            m_icons.emplace_back(make_tuple(texture, entity->GetPosition()));
                        }
                    }
                }
            }
        }

        if (!m_icons.empty())
        { 
            cmd_list->BeginTimeblock("icons");
            {
                // set pipeline state
                RHI_PipelineState pso;
                pso.name                              = "icons";
                pso.shaders[RHI_Shader_Type::Compute] = GetShader(Renderer_Shader::icon_c);
                cmd_list->SetPipelineState(pso);

                // bind output texture
                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);

                // lambda to dispatch a single icon
                auto dispatch_icon = [&](RHI_Texture* texture, const math::Vector3& pos_world)
                {
                     // set push constants: world position and texture dimensions
                    m_pcb_pass_cpu.set_f3_value(pos_world.x, pos_world.y, pos_world.z);
                    m_pcb_pass_cpu.set_f2_value(static_cast<float>(texture->GetWidth()), static_cast<float>(texture->GetHeight()));
                    cmd_list->PushConstants(m_pcb_pass_cpu);

                    // bind icon texture
                    cmd_list->SetTexture(Renderer_BindingsSrv::tex, texture);

                    // dispatch compute shader
                    uint32_t thread_x = 32;
                    uint32_t thread_y = 32;
                    uint32_t groups_x = (texture->GetWidth() + thread_x - 1) / thread_x;  // ceil(width / thread_x)
                    uint32_t groups_y = (texture->GetHeight() + thread_y - 1) / thread_y; // ceil(height / thread_y)
                    cmd_list->Dispatch(groups_x, groups_y, 1);

                    // this is to avoid out of order UAV access and flickering overlapping icons
                    // ideally, we batch all the icons in one buffer and do a single dispatch, but for now this works
                    cmd_list->InsertBarrier(texture->GetRhiResource(), texture->GetFormat(), 0, 1, 1, texture->GetLayout(0));
                };

                // dispatch all icons in m_icons
                for (const auto& [texture, pos_world] : m_icons)
                {
                    if (texture)
                    {
                        dispatch_icon(texture, pos_world);
                    }
                }
            }
            cmd_list->EndTimeblock();
        }
    }

    void Renderer::Pass_Grid(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (!GetOption<bool>(Renderer_Option::Grid))
            return;

        // acquire resources
        RHI_Shader* shader_v = GetShader(Renderer_Shader::grid_v);
        RHI_Shader* shader_p = GetShader(Renderer_Shader::grid_p);

        cmd_list->BeginTimeblock("grid");

        // set pipeline state
        RHI_PipelineState pso;
        pso.name                             = "grid";
        pso.shaders[RHI_Shader_Type::Vertex] = shader_v;
        pso.shaders[RHI_Shader_Type::Pixel]  = shader_p;
        pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Solid);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Alpha);
        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadGreaterEqual);
        pso.render_target_color_textures[0]  = tex_out;
        pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_output);
        cmd_list->SetPipelineState(pso);

        // set transform
        {
            // follow camera in world unit increments so that the grid appears stationary in relation to the camera
            const float grid_spacing       = 1.0f;
            const Vector3& camera_position = World::GetCamera()->GetEntity()->GetPosition();
            const Vector3 translation      = Vector3(
                floor(camera_position.x / grid_spacing) * grid_spacing,
                0.0f,
                floor(camera_position.z / grid_spacing) * grid_spacing
            );

            m_pcb_pass_cpu.transform = Matrix::CreateScale(Vector3(1000.0f, 1.0f, 1000.0f)) * Matrix::CreateTranslation(translation);
            cmd_list->PushConstants(m_pcb_pass_cpu);
        }

        cmd_list->SetCullMode(RHI_CullMode::Back);
        cmd_list->SetBufferVertex(GetStandardMesh(MeshType::Quad)->GetVertexBuffer());
        cmd_list->SetBufferIndex(GetStandardMesh(MeshType::Quad)->GetIndexBuffer());
        cmd_list->DrawIndexed(6);

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Lines(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        RHI_Shader* shader_v  = GetShader(Renderer_Shader::line_v);
        RHI_Shader* shader_p  = GetShader(Renderer_Shader::line_p);
        uint32_t vertex_count = static_cast<uint32_t>(m_lines_vertices.size());

        if (vertex_count != 0)
        {
            cmd_list->BeginTimeblock("lines");

            // set pipeline state
            RHI_PipelineState pso;
            pso.name                             = "lines";
            pso.shaders[RHI_Shader_Type::Vertex] = shader_v;
            pso.shaders[RHI_Shader_Type::Pixel]  = shader_p;
            pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Wireframe);
            pso.blend_state                      = GetBlendState(Renderer_BlendState::Alpha);
            pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadGreaterEqual);
            pso.render_target_color_textures[0]  = tex_out;
            pso.clear_color[0]                   = rhi_color_load;
            pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_output);
            pso.primitive_toplogy                = RHI_PrimitiveTopology::LineList;
            cmd_list->SetPipelineState(pso);

            // grow vertex buffer (if needed) 
            if (vertex_count > m_lines_vertex_buffer->GetElementCount())
            {
                m_lines_vertex_buffer = make_shared<RHI_Buffer>(RHI_Buffer_Type::Vertex, sizeof(m_lines_vertices[0]), vertex_count, static_cast<void*>(&m_lines_vertices[0]), true, "lines");
            }

            // update and set vertex buffer
            RHI_Vertex_PosCol* buffer = static_cast<RHI_Vertex_PosCol*>(m_lines_vertex_buffer->GetMappedData());
            memset(buffer, 0, m_lines_vertex_buffer->GetObjectSize());
            copy(m_lines_vertices.begin(), m_lines_vertices.end(), buffer);
            cmd_list->SetBufferVertex(m_lines_vertex_buffer.get());

            cmd_list->SetCullMode(RHI_CullMode::None);
            cmd_list->Draw(static_cast<uint32_t>(m_lines_vertices.size()));
            cmd_list->SetCullMode(RHI_CullMode::Back);

            cmd_list->EndTimeblock();
        }
    }

    void Renderer::Pass_Outline(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (!GetOption<bool>(Renderer_Option::SelectionOutline) || Engine::IsFlagSet(EngineMode::Playing))
            return;

        // acquire shaders
        RHI_Shader* shader_v = GetShader(Renderer_Shader::outline_v);
        RHI_Shader* shader_p = GetShader(Renderer_Shader::outline_p);
        RHI_Shader* shader_c = GetShader(Renderer_Shader::outline_c);

        if (Camera* camera = World::GetCamera())
        {
            if (shared_ptr<Entity> entity_selected = camera->GetSelectedEntity())
            {
                cmd_list->BeginTimeblock("outline");
                {
                    RHI_Texture* tex_outline = GetRenderTarget(Renderer_RenderTarget::outline);

                    if (Renderable* renderable = entity_selected->GetComponent<Renderable>())
                    {
                        cmd_list->BeginMarker("color_silhouette");
                        {
                            // set pipeline state
                            RHI_PipelineState pso;
                            pso.name                             = "color_silhouette";
                            pso.shaders[RHI_Shader_Type::Vertex] = shader_v;
                            pso.shaders[RHI_Shader_Type::Pixel]  = shader_p;
                            pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Solid);
                            pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
                            pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::Off);
                            pso.render_target_color_textures[0]  = tex_outline;
                            pso.clear_color[0]                   = Color::standard_transparent;
                            cmd_list->SetPipelineState(pso);
                        
                            // render
                            {
                                // push draw data
                                m_pcb_pass_cpu.set_f4_value(Color::standard_renderer_lines);
                                m_pcb_pass_cpu.transform = entity_selected->GetMatrix();
                                cmd_list->PushConstants(m_pcb_pass_cpu);
                        
                                cmd_list->SetBufferVertex(renderable->GetVertexBuffer());
                                cmd_list->SetBufferIndex(renderable->GetIndexBuffer());
                                cmd_list->DrawIndexed(renderable->GetIndexCount(), renderable->GetIndexOffset(), renderable->GetVertexOffset());
                            }
                        }
                        cmd_list->EndMarker();
                        
                        // blur the color silhouette
                        {
                            const float radius = 30.0f;
                            Pass_Blur(cmd_list, tex_outline, radius);
                        }
                        
                        // combine color silhouette with frame
                        cmd_list->BeginMarker("composition");
                        {
                            // set pipeline state
                            RHI_PipelineState pso;
                            pso.name             = "composition";
                            pso.shaders[Compute] = shader_c;
                            cmd_list->SetPipelineState(pso);
                        
                            // set textures
                            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
                            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_outline);
                        
                            // render
                            cmd_list->Dispatch(tex_out);
                        }
                        cmd_list->EndMarker();
                    }
                }
                cmd_list->EndTimeblock();
            }
        }
    }

    void Renderer::Pass_Text(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        // acquire resources
        const bool draw       = GetOption<bool>(Renderer_Option::PerformanceMetrics);
        const auto& shader_v  = GetShader(Renderer_Shader::font_v);
        const auto& shader_p  = GetShader(Renderer_Shader::font_p);
        shared_ptr<Font> font = GetFont();

        if (!font->HasText())
            return;

        cmd_list->BeginTimeblock("text");

        font->UpdateVertexAndIndexBuffers(cmd_list);

        // define pipeline state
        RHI_PipelineState pso;
        pso.name                             = "text";
        pso.shaders[RHI_Shader_Type::Vertex] = shader_v;
        pso.shaders[RHI_Shader_Type::Pixel]  = shader_p;
        pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Solid);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Alpha);
        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::Off);
        pso.render_target_color_textures[0]  = tex_out;
        pso.clear_color[0]                   = rhi_color_load;

        // set shared state
        cmd_list->SetPipelineState(pso);
        cmd_list->SetBufferVertex(font->GetVertexBuffer());
        cmd_list->SetBufferIndex(font->GetIndexBuffer());
        cmd_list->SetCullMode(RHI_CullMode::Back);

        // draw outline
        if (font->GetOutline() != Font_Outline_None && font->GetOutlineSize() != 0)
        {
            m_pcb_pass_cpu.set_f4_value(font->GetColorOutline());
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, font->GetAtlasOutline().get());
            cmd_list->DrawIndexed(font->GetIndexCount());
        }

        // draw inline
        {
            m_pcb_pass_cpu.set_f4_value(font->GetColor());
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, font->GetAtlas().get());
            cmd_list->DrawIndexed(font->GetIndexCount());
        }

        cmd_list->EndTimeblock();
    }
}
