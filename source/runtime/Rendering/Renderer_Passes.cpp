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
#include "../RHI/RHI_AccelerationStructure.h"
#include "../Rendering/Material.h"
#include "../RHI/RHI_VendorTechnology.h"
#include "../RHI/RHI_RasterizerState.h"
#include "../RHI/RHI_Device.h"
#include "../Core/Window.h"
SP_WARNINGS_OFF
#include "bend_sss_cpu.h"
SP_WARNINGS_ON
//==========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    array<Renderer_DrawCall, renderer_max_draw_calls> Renderer::m_draw_calls;
    uint32_t Renderer::m_draw_call_count;
    array<Renderer_DrawCall, renderer_max_draw_calls> Renderer::m_draw_calls_prepass;
    uint32_t Renderer::m_draw_calls_prepass_count;
    unique_ptr<RHI_Buffer> Renderer::m_std_reflections;
    unique_ptr<RHI_Buffer> Renderer::m_std_shadows;
    unique_ptr<RHI_Buffer> Renderer::m_std_restir;

    void Renderer::SetStandardResources(RHI_CommandList* cmd_list)
    {
        cmd_list->SetConstantBuffer(Renderer_BindingsCb::frame, GetBuffer(Renderer_Buffer::ConstantFrame));
        cmd_list->SetTexture(Renderer_BindingsSrv::tex_perlin, GetStandardTexture(Renderer_StandardTexture::Noise_perlin));
    }

    void Renderer::ProduceFrame(RHI_CommandList* cmd_list_graphics_present, RHI_CommandList* cmd_list_compute)
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

        // generate BRDF LUT (once per session)
        static bool brdf_produced = false;
        if (!brdf_produced)
        {
            Pass_Lut_BrdfSpecular(cmd_list_graphics_present);
            brdf_produced = true;
        }

        // generate cloud noise textures (once at startup, before skysphere needs them)
        Pass_CloudNoise(cmd_list_graphics_present);

        // Skysphere update logic:
        // - Always render once on startup (for initial clouds)
        // - Re-render when light changes
        // - Re-render every frame if cloud animation is enabled
        // - Run for multiple frames after change for temporal accumulation to converge
        //   (checkerboard needs 2 frames, temporal blend ~0.2 needs ~5-8 frames = 8 total)
        bool clouds_enabled = cvar_clouds_enabled.GetValueAs<bool>();
        bool clouds_visible = clouds_enabled && cvar_cloud_coverage.GetValue() > 0.0f;
        bool cloud_animation = clouds_enabled && cvar_cloud_animation.GetValue() > 0.0f;
        
        {
            bool update_skysphere = false;
            Light* directional_light = World::GetDirectionalLight();
            
            {
                static bool first_frame = true;
                static bool had_directional_light = false;
                static bool last_clouds_enabled = false;
                static float last_coverage = -1.0f;
                static float last_seed = -1.0f;
                static float last_cloud_type = -1.0f;
                static float last_darkness = -1.0f;
                static uint32_t frames_remaining = 0; // temporal convergence counter
                const uint32_t temporal_convergence_frames = 8; // frames needed for checkerboard + temporal blend
                
                bool has_directional_light = directional_light != nullptr;
                float current_coverage = cvar_cloud_coverage.GetValue();
                float current_seed = cvar_cloud_seed.GetValue();
                float current_type = cvar_cloud_type.GetValue();
                float current_darkness = cvar_cloud_darkness.GetValue();
                
                // Update skysphere when:
                // 1. First frame (initial render)
                // 2. Light changes
                // 3. Cloud parameters changed (enabled, coverage, seed, type, darkness)
                // 4. Cloud animation is enabled (for wind movement)
                // 5. Temporal convergence still in progress
                bool light_changed = (has_directional_light && directional_light->NeedsSkysphereUpdate()) || 
                                     (has_directional_light != had_directional_light);
                bool cloud_params_changed = (clouds_enabled != last_clouds_enabled) ||
                                            (current_coverage != last_coverage) ||
                                            (current_seed != last_seed) ||
                                            (current_type != last_cloud_type) ||
                                            (current_darkness != last_darkness);
                
                // reset convergence counter when something changes
                if (first_frame || light_changed || cloud_params_changed)
                {
                    frames_remaining = temporal_convergence_frames;
                }
                
                // update if animating, converging, or something changed
                update_skysphere = cloud_animation || (frames_remaining > 0);
                
                // decrement convergence counter
                if (frames_remaining > 0)
                {
                    frames_remaining--;
                }
                
                first_frame = false;
                had_directional_light = has_directional_light;
                last_clouds_enabled = clouds_enabled;
                last_coverage = current_coverage;
                last_seed = current_seed;
                last_cloud_type = current_type;
                last_darkness = current_darkness;
            }
            
            if (update_skysphere)
            {
                // Only update LUT when light changes (it's expensive)
                static bool lut_generated = false;
                if (!lut_generated || (directional_light && directional_light->NeedsSkysphereUpdate()))
                {
                    Pass_Lut_AtmosphericScattering(cmd_list_graphics_present);
                    lut_generated = true;
                }
                Pass_Skysphere(cmd_list_graphics_present);
            }
        }

        // Only update cloud shadow map when clouds are visible
        if (clouds_visible)
        {
            Pass_CloudShadow(cmd_list_graphics_present);
        }

        if (Camera* camera = World::GetCamera())
        {
            Pass_VariableRateShading(cmd_list_graphics_present);

            // opaques
            {
                bool is_transparent = false;
                Pass_Occlusion(cmd_list_graphics_present);
                Pass_Depth_Prepass(cmd_list_graphics_present);
                Pass_GBuffer(cmd_list_graphics_present, is_transparent);
                Pass_ShadowMaps(cmd_list_graphics_present);
                Pass_ScreenSpaceShadows(cmd_list_graphics_present);
                Pass_RayTracedShadows(cmd_list_graphics_present);
                Pass_ReSTIR_PathTracing(cmd_list_graphics_present);
                Pass_ScreenSpaceAmbientOcclusion(cmd_list_graphics_present);
                Pass_Light(cmd_list_graphics_present, is_transparent);
                Pass_Light_Composition(cmd_list_graphics_present, is_transparent);
                cmd_list_graphics_present->Blit(GetRenderTarget(Renderer_RenderTarget::frame_render), GetRenderTarget(Renderer_RenderTarget::frame_render_opaque), false);
            }

            // transparents
            if (m_transparents_present)
            {
                bool is_transparent = true;
                Pass_GBuffer(cmd_list_graphics_present, is_transparent);
                Pass_Light(cmd_list_graphics_present, is_transparent);
                Pass_Light_Composition(cmd_list_graphics_present, is_transparent);
            }

            Pass_Light_ImageBased(cmd_list_graphics_present);
            
            // ray traced reflections run after transparent g-buffer so depth includes transparents
            // this way opaques behind glass don't trace rays (only visible surfaces get reflections)
            Pass_RayTracedReflections(cmd_list_graphics_present);
            Pass_Light_Reflections(cmd_list_graphics_present);
            
            Pass_TransparencyReflectionRefraction(cmd_list_graphics_present);
            Pass_AA_Upscale(cmd_list_graphics_present);
            Pass_PostProcess(cmd_list_graphics_present);
        }
        else
        {
            cmd_list_graphics_present->ClearTexture(rt_output, Color::standard_black);
        }

        Pass_Text(cmd_list_graphics_present, rt_output);

        // perform early transitions (so the next frame doesn't have to wait)
        rt_output->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list_graphics_present);
        GetRenderTarget(Renderer_RenderTarget::gbuffer_color)->SetLayout(RHI_Image_Layout::Attachment, cmd_list_graphics_present);
        GetRenderTarget(Renderer_RenderTarget::gbuffer_normal)->SetLayout(RHI_Image_Layout::Attachment, cmd_list_graphics_present);
        GetRenderTarget(Renderer_RenderTarget::gbuffer_material)->SetLayout(RHI_Image_Layout::Attachment, cmd_list_graphics_present);
        GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity)->SetLayout(RHI_Image_Layout::Attachment, cmd_list_graphics_present);
        GetRenderTarget(Renderer_RenderTarget::gbuffer_depth)->SetLayout(RHI_Image_Layout::Attachment, cmd_list_graphics_present);
    }

    void Renderer::Pass_VariableRateShading(RHI_CommandList* cmd_list)
    {
        if (!cvar_variable_rate_shading.GetValueAs<bool>())
            return;

        // acquire resources
        RHI_Shader* shader_c = GetShader(Renderer_Shader::variable_rate_shading_c);
        RHI_Texture* tex_in  = GetRenderTarget(Renderer_RenderTarget::frame_output);
        RHI_Texture* tex_out = GetRenderTarget(Renderer_RenderTarget::shading_rate);
        if (!shader_c || !shader_c->IsCompiled() || !tex_in || !tex_out)
            return;

        // clear to full rate (0 = 1x1) to ensure safe initial values when vrs is first enabled
        // we track this per-texture since render targets can be recreated on resolution changes
        static RHI_Texture* last_cleared_texture = nullptr;
        if (tex_out != last_cleared_texture)
        {
            cmd_list->ClearTexture(tex_out, Color(0.0f, 0.0f, 0.0f, 0.0f));
            last_cleared_texture = tex_out;
        }

        cmd_list->BeginTimeblock("variable_rate_shading");
        {
            // set pipeline state
            RHI_PipelineState pso;
            pso.name             = "variable_rate_shading";
            pso.shaders[Compute] = shader_c;
            cmd_list->SetPipelineState(pso);

            // set textures (uses previous frame's output for temporal feedback)
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            cmd_list->SetTexture(Renderer_BindingsUav::tex_uint, tex_out);

            // render
            cmd_list->Dispatch(tex_out);
        }
        cmd_list->EndTimeblock();
    }
  
    void Renderer::Pass_ShadowMaps(RHI_CommandList* cmd_list)
    {
        if (World::GetLightCount() == 0)
            return;

        // define base pipeline state
        RHI_PipelineState pso;
        pso.name                             = "shadow_maps";
        pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::depth_light_v);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
        pso.clear_depth                      = 0.0f;
        pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::shadow_atlas);
        pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Light_directional); // the world always starts with the directional lght

        cmd_list->BeginTimeblock(pso.name);
        {
            // set base state
            cmd_list->SetPipelineState(pso);

            // render shadow maps using cached renderables
            for (Entity* entity_light : World::GetEntitiesLights())
            {
                Light* light = entity_light->GetComponent<Light>();
                if (!light->GetFlag(LightFlags::Shadows) || light->GetIntensityWatt() == 0.0f)
                    continue;
    
                // set rasterizer state
                RHI_RasterizerState* new_state = (light->GetLightType() == LightType::Directional) ? GetRasterizerState(Renderer_RasterizerState::Light_directional) : GetRasterizerState(Renderer_RasterizerState::Light_point_spot);
                if (pso.rasterizer_state != new_state)
                {
                    pso.rasterizer_state = new_state;
                    cmd_list->SetPipelineState(pso);
                }

                // iterate over slices (all lights are just texture arrays)
                for (uint32_t array_index = 0; array_index < light->GetSliceCount(); array_index++)
                {
                    // get atlas rectangle for this slice
                    const math::Rectangle& rect = light->GetAtlasRectangle(array_index);
                    if (!rect.IsDefined()) // can happen if there is no more atlas space
                        continue;

                    // set atlas rectangle as viewport and scissor
                    static RHI_Viewport viewport;
                    viewport.x      = rect.x;
                    viewport.y      = rect.y;
                    viewport.width  = rect.width;
                    viewport.height = rect.height;
                    cmd_list->SetViewport(viewport);
                    cmd_list->SetScissorRectangle(rect);

                    // render cached renderables
                    for (uint32_t i = 0; i < m_draw_call_count; i++)
                    {
                        const Renderer_DrawCall& draw_call = m_draw_calls[i];
                        Renderable* renderable             = draw_call.renderable;
                        Material* material                 = renderable->GetMaterial();
                        const float shadow_distance        = renderable->GetMaxShadowDistance();
                        if (!material || material->IsTransparent() || !renderable->HasFlag(RenderableFlags::CastsShadows) || draw_call.distance_squared > shadow_distance * shadow_distance)
                            continue;

                        // todo: this needs to be recalculate only when the light or the renderable moves, not every frame
                        if (!light->IsInViewFrustum(renderable, array_index))
                            continue;

                        // pixel shader
                        {
                            bool is_first_cascade = array_index == 0 && light->GetLightType() == LightType::Directional;
                            bool is_alpha_tested  = material->IsAlphaTested();
                            RHI_Shader* ps        = (is_first_cascade && is_alpha_tested) ? GetShader(Renderer_Shader::depth_light_alpha_color_p) : nullptr;
                        
                            if (pso.shaders[RHI_Shader_Type::Pixel] != ps)
                            {
                                pso.shaders[RHI_Shader_Type::Pixel] = ps;
                                cmd_list->SetPipelineState(pso);

                                // if the pipeline changed, set the viewport and scissor again
                                cmd_list->SetViewport(viewport);
                                cmd_list->SetScissorRectangle(rect);
                            }
                        }

                        // push constants
                        m_pcb_pass_cpu.transform = renderable->GetEntity()->GetMatrix();
                        m_pcb_pass_cpu.set_f3_value(material->HasTextureOfType(MaterialTextureType::Color) ? 1.0f : 0.0f);
                        m_pcb_pass_cpu.set_f3_value2(static_cast<float>(light->GetIndex()), static_cast<float>(array_index), 0.0f);
                        m_pcb_pass_cpu.set_is_transparent_and_material_index(false, material->GetIndex());
                        cmd_list->PushConstants(m_pcb_pass_cpu);
    
                        // draw
                        {
                            cmd_list->SetCullMode(static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)));
                            cmd_list->SetBufferVertex(renderable->GetVertexBuffer(), renderable->GetInstanceBuffer());
                            cmd_list->SetBufferIndex(renderable->GetIndexBuffer());

                            // compute lod index
                            bool close_to_shadow      = renderable->GetDistanceSquared() < 100.0f * 100.0f;                                   // anything within 100 meters of the shadow caster
                            uint32_t lod_index_bias   = light->GetLightType() == LightType::Directional ? 1 : 0;                              // bias for directional lights
                            uint32_t lod_index_shadow = clamp(renderable->GetLodIndex() + lod_index_bias, 0u, renderable->GetLodCount() - 1); // lod index biased towards lower quality lod
                            uint32_t lod_index        = close_to_shadow ? draw_call.lod_index : lod_index_shadow;                             // use normal lod if close to shadow caster, otherwise use light specific lod

                            cmd_list->DrawIndexed(
                                renderable->GetIndexCount(lod_index),
                                renderable->GetIndexOffset(lod_index),
                                renderable->GetVertexOffset(lod_index),
                                draw_call.instance_index,
                                draw_call.instance_count
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
        // determines visibility without GPU stalls
        // major occluders are rendered to a depth buffer, then a Hi-Z mip chain enables fast coarse AABB tests
        // objects failing Hi-Z but recently visible get precise occlusion queries, with results read next frame
        // recently visible objects are drawn until confirmed occluded, avoiding sudden disappearances

        if (!cvar_occlusion_culling.GetValueAs<bool>())
            return;
    
        cmd_list->BeginTimeblock("occlusion");
    
        // persistent visibility state across frames (since draw calls rebuild each frame)
        struct VisibilityState
        {
            bool     pending_query      = false;
            uint32_t last_visible_frame = 0;
        };
        static unordered_map<uint64_t, VisibilityState> visibility_states;
    
        // check pending queries from previous frame and update visibility
        for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
        {
            Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];
            uint64_t entity_id           = draw_call.renderable->GetEntity()->GetObjectId();
            auto& state                  = visibility_states[entity_id]; // creates if missing
    
            if (state.pending_query)
            {
                if (cmd_list->GetOcclusionQueryResult(entity_id)) // occluded
                {
                    // set invisible
                    draw_call.camera_visible = false;
                    draw_call.renderable->SetVisible(false);
                }
                else // visible or not ready
                {
                    // stay/set visible
                    draw_call.camera_visible = true;
                    state.last_visible_frame = m_cb_frame_cpu.frame;
                    draw_call.renderable->SetVisible(true);
                }
    
                state.pending_query = false;
            }
        }
    
        // get resources
        RHI_Texture* tex_occluders     = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_occluders);
        RHI_Texture* tex_occluders_hiz = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_occluders_hiz);
    
        // render the occluders
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
    
            for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
            {
                const Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];
    
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
        Pass_Downscale(cmd_list, tex_occluders_hiz, Renderer_DownsampleFilter::Max);
    
        // do the actual occlusion
        {
            // define pipeline state
            RHI_PipelineState pso;
            pso.name             = "occlusion";
            pso.shaders[Compute] = GetShader(Renderer_Shader::occlusion_c);
    
            cmd_list->SetPipelineState(pso);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_occluders_hiz);
    
            // set aabb count
            m_pcb_pass_cpu.set_f4_value(GetViewport().width, GetViewport().height, static_cast<float>(m_draw_calls_prepass_count), static_cast<float>(tex_occluders_hiz->GetMipCount()));
            cmd_list->PushConstants(m_pcb_pass_cpu);
    
            // set the visibility buffer (where the occlusion results will be written)
            cmd_list->SetBuffer(Renderer_BindingsUav::visibility, GetBuffer(Renderer_Buffer::Visibility));
    
            // clearing and hi-jacking the diffuse texture - just for debugging purposes
            cmd_list->ClearTexture(GetRenderTarget(Renderer_RenderTarget::light_diffuse), Color::standard_black);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, GetRenderTarget(Renderer_RenderTarget::light_diffuse));
    
            // dispatch: ceil(aabb_count / 256) thread groups
            uint32_t thread_group_count = (m_draw_calls_prepass_count + 255) / 256; // ceiling division
            cmd_list->Dispatch(thread_group_count, 1, 1);
        }
    
        // update the draw calls with the previous frame's visibility results
        uint32_t* visibility_data = static_cast<uint32_t*>(GetBuffer(Renderer_Buffer::VisibilityPrevious)->GetMappedData());
        for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
        {
            Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];
            uint64_t entity_id           = draw_call.renderable->GetEntity()->GetObjectId();
            auto& state                  = visibility_states[entity_id]; // creates if missing
    
            if (!draw_call.is_occluder && draw_call.camera_visible)
            {
                bool hi_z_visible = visibility_data[i];

                if (hi_z_visible)
                {
                    draw_call.camera_visible = true;
                    state.last_visible_frame = m_cb_frame_cpu.frame;
                }
                else if (state.last_visible_frame >= m_cb_frame_cpu.frame - 1)
                {
                    // conservative: draw and query, but don't update last_visible_frame (wait for confirmation)
                    draw_call.camera_visible = true;
                
                    // issue query
                    RHI_PipelineState query_pso;
                    query_pso.name                             = "occlusion_query";
                    query_pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::depth_prepass_v); // reuse for mesh
                    query_pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Solid);
                    query_pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
                    query_pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadGreaterEqual); // default read func
                    query_pso.render_target_depth_texture      = tex_occluders; // test against current occluders
                    cmd_list->SetPipelineState(query_pso);
                
                    // culling (match occluder logic)
                    Renderable* renderable = draw_call.renderable;
                    RHI_CullMode cull_mode = static_cast<RHI_CullMode>(renderable->GetMaterial()->GetProperty(MaterialProperty::CullMode));
                    cull_mode              = (query_pso.rasterizer_state->GetPolygonMode() == RHI_PolygonMode::Wireframe) ? RHI_CullMode::None : cull_mode;
                    cmd_list->SetCullMode(cull_mode);
                
                    // set pass constants
                    m_pcb_pass_cpu.transform = renderable->GetEntity()->GetMatrix();
                    cmd_list->PushConstants(m_pcb_pass_cpu);
                
                    // draw mesh with occlusion query
                    cmd_list->BeginOcclusionQuery(entity_id);
                    cmd_list->SetBufferVertex(renderable->GetVertexBuffer());
                    cmd_list->SetBufferIndex(renderable->GetIndexBuffer());
                    cmd_list->DrawIndexed(
                        renderable->GetIndexCount(draw_call.lod_index),
                        renderable->GetIndexOffset(draw_call.lod_index),
                        renderable->GetVertexOffset(draw_call.lod_index)
                    );
                    cmd_list->EndOcclusionQuery();
                
                    state.pending_query = true;
                }
                else
                {
                    // safe to cull: Hi-Z occluded and not recently visible
                    draw_call.camera_visible = false;
                }
                
                draw_call.renderable->SetVisible(draw_call.camera_visible);
            }
        }

        // swap visibility buffers for next frame (ping-pong)
        SwapVisibilityBuffers();
    
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Depth_Prepass(RHI_CommandList* cmd_list)
    {
        // acquire resources
        RHI_Texture* tex_depth        = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);               // render resolution - base depth
        RHI_Texture* tex_depth_output = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output); // output resolution
   
        // deduce rasterizer state
        bool is_wireframe                     = cvar_wireframe.GetValueAs<bool>();
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
            pso.vrs_input_texture                = cvar_variable_rate_shading.GetValueAs<bool>() ? GetRenderTarget(Renderer_RenderTarget::shading_rate) : nullptr;
            pso.render_target_depth_texture      = tex_depth;
            pso.resolution_scale                 = true;
            pso.clear_depth                      = 0.0f;
            cmd_list->SetPipelineState(pso);

            for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
            {
                const Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];
                Renderable* renderable             = draw_call.renderable;
                Material* material                 = renderable->GetMaterial();
                if (!material || material->IsTransparent() || !draw_call.camera_visible)
                    continue;
    
                // alpha testing & tessellation
                {
                    bool tessellated = material->GetProperty(MaterialProperty::Tessellation) > 0.0f;
                    RHI_Shader* ps   = material->IsAlphaTested() ? GetShader(Renderer_Shader::depth_prepass_alpha_test_p) : nullptr;
                    RHI_Shader* hs   = tessellated ? GetShader(Renderer_Shader::tessellation_h) : nullptr;
                    RHI_Shader* ds   = tessellated ? GetShader(Renderer_Shader::tessellation_d) : nullptr;

                    if (pso.shaders[RHI_Shader_Type::Pixel]  != ps || pso.shaders[RHI_Shader_Type::Hull] != hs ||  pso.shaders[RHI_Shader_Type::Domain] != ds)
                    {
                        pso.shaders[RHI_Shader_Type::Pixel]  = ps;
                        pso.shaders[RHI_Shader_Type::Hull]   = hs;
                        pso.shaders[RHI_Shader_Type::Domain] = ds;
                        cmd_list->SetPipelineState(pso);
                    }
                }
    
                // pass constants
                {
                    bool has_color_texture = material->HasTextureOfType(MaterialTextureType::Color);
                    m_pcb_pass_cpu.set_f3_value(0.0f, has_color_texture ? 1.0f : 0.0f, static_cast<float>(i));
                    m_pcb_pass_cpu.set_is_transparent_and_material_index(false, material->GetIndex());
                    m_pcb_pass_cpu.transform = renderable->GetEntity()->GetMatrix();
                    cmd_list->PushConstants(m_pcb_pass_cpu);
                }

                // draw
                {
                    RHI_CullMode cull_mode = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode));
                    cull_mode              = (pso.rasterizer_state->GetPolygonMode() == RHI_PolygonMode::Wireframe) ? RHI_CullMode::None : cull_mode;
                    cmd_list->SetCullMode(cull_mode);
                    cmd_list->SetBufferVertex(renderable->GetVertexBuffer(), renderable->GetInstanceBuffer());
                    cmd_list->SetBufferIndex(renderable->GetIndexBuffer());

                    cmd_list->DrawIndexed(
                        renderable->GetIndexCount(draw_call.lod_index),
                        renderable->GetIndexOffset(draw_call.lod_index),
                        renderable->GetVertexOffset(draw_call.lod_index),
                        draw_call.instance_index,
                        draw_call.instance_count
                    );

                    // at this point, we don't want clear in case another render pass is implicitly started
                    pso.clear_depth = rhi_depth_load;
                }
            }

            // blit to output resolution
            float resolution_scale = cvar_resolution_scale.GetValue();
            cmd_list->Blit(tex_depth, tex_depth_output, false, resolution_scale);
    
            // perform early resource transitions
            {
                tex_depth->SetLayout(RHI_Image_Layout::Attachment, cmd_list);
                tex_depth_output->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
                cmd_list->FlushBarriers();
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
        {
            // set pipeline state
            RHI_PipelineState pso;
            pso.name                             = is_transparent_pass ? "g_buffer_transparent" : "g_buffer";
            pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::gbuffer_v);
            pso.shaders[RHI_Shader_Type::Pixel]  = GetShader(Renderer_Shader::gbuffer_p);
            pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
            pso.rasterizer_state                 = cvar_wireframe.GetValueAs<bool>() ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : GetRasterizerState(Renderer_RasterizerState::Solid);
            pso.depth_stencil_state              = is_transparent_pass ? GetDepthStencilState(Renderer_DepthStencilState::ReadWrite) : GetDepthStencilState(Renderer_DepthStencilState::ReadEqual); // transparents are see-through, no pre-pass needed
            pso.vrs_input_texture                = cvar_variable_rate_shading.GetValueAs<bool>() ? GetRenderTarget(Renderer_RenderTarget::shading_rate) : nullptr;
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
            cmd_list->SetPipelineState(pso);

            for (uint32_t i = 0; i < m_draw_call_count; i++)
            {
                const Renderer_DrawCall& draw_call = m_draw_calls[i];
                Renderable* renderable             = draw_call.renderable;
                Material* material                 = renderable->GetMaterial();
                if (!material || material->IsTransparent() != is_transparent_pass || !draw_call.camera_visible)
                    continue;
    
                // tessellation & culling
                {
                    bool is_tessellated = material->GetProperty(MaterialProperty::Tessellation) > 0.0f;
                    RHI_Shader* hull    = is_tessellated ? GetShader(Renderer_Shader::tessellation_h) : nullptr;
                    RHI_Shader* domain  = is_tessellated ? GetShader(Renderer_Shader::tessellation_d) : nullptr;
                
                    if (pso.shaders[RHI_Shader_Type::Hull] != hull || pso.shaders[RHI_Shader_Type::Domain] != domain)
                    {
                        pso.shaders[RHI_Shader_Type::Hull]   = hull;
                        pso.shaders[RHI_Shader_Type::Domain] = domain;
                        cmd_list->SetPipelineState(pso);
                    }
                }

                // pass constants
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
                    cmd_list->SetCullMode(cvar_wireframe.GetValueAs<bool>() ? RHI_CullMode::None : static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)));
                    cmd_list->SetBufferVertex(renderable->GetVertexBuffer(), renderable->GetInstanceBuffer());
                    cmd_list->SetBufferIndex(renderable->GetIndexBuffer());
    
                    cmd_list->DrawIndexed(
                        renderable->GetIndexCount(draw_call.lod_index),
                        renderable->GetIndexOffset(draw_call.lod_index),
                        renderable->GetVertexOffset(draw_call.lod_index),
                        draw_call.instance_index,
                        draw_call.instance_count
                    );

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
            cmd_list->FlushBarriers();
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_ScreenSpaceAmbientOcclusion(RHI_CommandList* cmd_list)
    {
        if (!cvar_ssao.GetValueAs<bool>())
            return;
            
        RHI_Texture* tex_ssao = GetRenderTarget(Renderer_RenderTarget::ssao);
        if (!tex_ssao)
            return;

        RHI_PipelineState pso;
        pso.name             = "screen_space_ambient_occlusion";
        pso.shaders[Compute] = GetShader(Renderer_Shader::ssao_c);

        cmd_list->BeginTimeblock(pso.name);
        {
            cmd_list->SetPipelineState(pso);
            SetCommonTextures(cmd_list);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_ssao);
            cmd_list->Dispatch(tex_ssao, cvar_resolution_scale.GetValue());
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_TransparencyReflectionRefraction(RHI_CommandList* cmd_list)
    {
        static bool cleared = false;

        RHI_Texture* tex_frame             = GetRenderTarget(Renderer_RenderTarget::frame_render);
        RHI_Texture* tex_ssr               = GetRenderTarget(Renderer_RenderTarget::reflections);
        RHI_Texture* tex_refraction_source = GetRenderTarget(Renderer_RenderTarget::frame_render_opaque);

        cmd_list->BeginTimeblock("transparency_reflection_refraction");
        {
            bool use_ray_traced = cvar_ray_traced_reflections.GetValueAs<bool>();

             if (!cleared && !use_ray_traced)
            {
                // only clear if neither ssr nor ray traced reflections wrote to this texture
                cmd_list->ClearTexture(tex_ssr, Color::standard_transparent);
                cleared = true;
            }

            cmd_list->InsertBarrier(tex_frame, RHI_BarrierType::EnsureReadThenWrite);

            cmd_list->BeginMarker("apply");
            {
                RHI_PipelineState pso;
                pso.name             = "apply_reflections_refraction";
                pso.shaders[Compute] = GetShader(Renderer_Shader::transparency_reflection_refraction_c);

                cmd_list->SetPipelineState(pso);
                SetCommonTextures(cmd_list);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex,  tex_ssr);               // in - reflection
                cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_refraction_source); // in - refraction
                cmd_list->SetTexture(Renderer_BindingsSrv::tex3, GetRenderTarget(Renderer_RenderTarget::lut_brdf_specular));
                cmd_list->SetTexture(Renderer_BindingsSrv::tex4, GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output));
                cmd_list->SetTexture(Renderer_BindingsUav::tex,  tex_frame);             // out
                cmd_list->Dispatch(tex_frame);
            }
            cmd_list->EndMarker();
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_RayTracedReflections(RHI_CommandList* cmd_list)
    {
        // skip when window is minimized or resolution is too small (e.g. during minimize animation)
        const uint32_t min_rt_dimension = 64;
        if (Window::IsMinimized())
            return;

        RHI_Texture* tex_reflections          = GetRenderTarget(Renderer_RenderTarget::reflections);
        RHI_Texture* tex_reflections_position = GetRenderTarget(Renderer_RenderTarget::gbuffer_reflections_position);
        RHI_Texture* tex_reflections_normal   = GetRenderTarget(Renderer_RenderTarget::gbuffer_reflections_normal);
        RHI_Texture* tex_reflections_albedo   = GetRenderTarget(Renderer_RenderTarget::gbuffer_reflections_albedo);

        // skip if render targets have invalid or too-small dimensions
        if (tex_reflections_position && (tex_reflections_position->GetWidth() < min_rt_dimension || tex_reflections_position->GetHeight() < min_rt_dimension))
            return;

        // clear reflections once when disabled, then skip
        static bool cleared = false;
        if (!cvar_ray_traced_reflections.GetValueAs<bool>() || !tex_reflections_position)
        {
            if (!cleared)
            {
                cmd_list->ClearTexture(tex_reflections, Color::standard_black);
                cleared = true;
            }
            return;
        }
        cleared = false;

        cmd_list->BeginTimeblock("ray_traced_reflections");
        {
            RHI_AccelerationStructure* tlas = GetTopLevelAccelerationStructure();
            if (!tlas || !tlas->GetRhiResource())
            {
                tex_reflections->SetLayout(RHI_Image_Layout::General, cmd_list);
                cmd_list->ClearTexture(tex_reflections, Color(1.0f, 1.0f, 0.0f, 1.0f));
                cmd_list->EndTimeblock();
                return;
            }

            // transition output textures to general layout for uav writes
            tex_reflections_position->SetLayout(RHI_Image_Layout::General, cmd_list);
            tex_reflections_normal->SetLayout(RHI_Image_Layout::General, cmd_list);
            tex_reflections_albedo->SetLayout(RHI_Image_Layout::General, cmd_list);
            cmd_list->InsertBarrier(tex_reflections_position, RHI_BarrierType::EnsureReadThenWrite);
            cmd_list->InsertBarrier(tex_reflections_normal, RHI_BarrierType::EnsureReadThenWrite);
            cmd_list->InsertBarrier(tex_reflections_albedo, RHI_BarrierType::EnsureReadThenWrite);

            // set pipeline state
            RHI_PipelineState pso;
            pso.name                   = "ray_traced_reflections";
            pso.shaders[RayGeneration] = GetShader(Renderer_Shader::reflections_ray_generation_r);
            pso.shaders[RayMiss]       = GetShader(Renderer_Shader::reflections_ray_miss_r);
            pso.shaders[RayHit]        = GetShader(Renderer_Shader::reflections_ray_hit_r);
            cmd_list->SetPipelineState(pso);

            // create or update shader binding table (must be after pipeline is set)
            if (!m_std_reflections)
            {
                uint32_t handle_size = RHI_Device::PropertyGetShaderGroupHandleSize();
                m_std_reflections = make_unique<RHI_Buffer>(RHI_Buffer_Type::ShaderBindingTable, handle_size, 3, nullptr, true, "reflections_sbt");
            }
            // update handles every frame in case pipeline changed (UpdateHandles needs pipeline to be set first)
            m_std_reflections->UpdateHandles(cmd_list);

            // set textures and acceleration structure
            SetCommonTextures(cmd_list);
            cmd_list->SetAccelerationStructure(Renderer_BindingsSrv::tlas, tlas);
            
            // ensure skysphere is in shader read layout for sampling
            RHI_Texture* tex_skysphere = GetRenderTarget(Renderer_RenderTarget::skysphere);
            tex_skysphere->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_skysphere);
            
            // geometry info buffer for vertex/index access in hit shader
            // reset offset to bind from start (Update advances offset for ring buffer pattern)
            GetBuffer(Renderer_Buffer::GeometryInfo)->ResetOffset();
            cmd_list->SetBuffer(Renderer_BindingsUav::geometry_info, GetBuffer(Renderer_Buffer::GeometryInfo));

            // set output textures (as UAVs for ray tracing write) - deferred g-buffer outputs
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex),  tex_reflections_position, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), tex_reflections_normal,   rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex3), tex_reflections_albedo,   rhi_all_mips, 0, true);

            // trace full screen (match tex resolution)
            uint32_t width  = tex_reflections_position->GetWidth();
            uint32_t height = tex_reflections_position->GetHeight();
            cmd_list->TraceRays(width, height, m_std_reflections.get());

            // ensure writes complete before the textures are read
            cmd_list->InsertBarrier(tex_reflections_position, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(tex_reflections_normal, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(tex_reflections_albedo, RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();
    }
    
    void Renderer::Pass_Light_Reflections(RHI_CommandList* cmd_list)
    {
        if (!cvar_ray_traced_reflections.GetValueAs<bool>())
            return;
            
        RHI_Texture* tex_reflections          = GetRenderTarget(Renderer_RenderTarget::reflections);
        RHI_Texture* tex_reflections_position = GetRenderTarget(Renderer_RenderTarget::gbuffer_reflections_position);
        RHI_Texture* tex_reflections_normal   = GetRenderTarget(Renderer_RenderTarget::gbuffer_reflections_normal);
        RHI_Texture* tex_reflections_albedo   = GetRenderTarget(Renderer_RenderTarget::gbuffer_reflections_albedo);
        RHI_Texture* tex_skysphere            = GetRenderTarget(Renderer_RenderTarget::skysphere);
        
        // gbuffer reflections are lazy allocated
        if (!tex_reflections_position)
            return;
        RHI_Texture* tex_shadow_atlas         = GetRenderTarget(Renderer_RenderTarget::shadow_atlas);
        
        cmd_list->BeginTimeblock("light_reflections");
        {
            // transition output texture for writing
            tex_reflections->SetLayout(RHI_Image_Layout::General, cmd_list);
            cmd_list->InsertBarrier(tex_reflections, RHI_BarrierType::EnsureReadThenWrite);
            
            // transition input textures for reading
            tex_reflections_position->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            tex_reflections_normal->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            tex_reflections_albedo->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            tex_skysphere->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            tex_shadow_atlas->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            
            // set pipeline state
            RHI_PipelineState pso;
            pso.name             = "light_reflections";
            pso.shaders[Compute] = GetShader(Renderer_Shader::light_reflections_c);
            cmd_list->SetPipelineState(pso);
            
            // set common textures (includes bindless materials)
            SetCommonTextures(cmd_list);
            
            // set input textures (reflection g-buffer)
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,  tex_reflections_position);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_reflections_normal);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_reflections_albedo);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex4, tex_skysphere);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex5, tex_shadow_atlas);
            
            // set output texture
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_reflections, rhi_all_mips, 0, true);
            
            // push constants: light count, skysphere mip count
            m_pcb_pass_cpu.set_f3_value(static_cast<float>(m_count_active_lights), static_cast<float>(tex_skysphere->GetMipCount()));
            cmd_list->PushConstants(m_pcb_pass_cpu);
            
            // dispatch
            cmd_list->Dispatch(tex_reflections);
            
            // ensure writes complete before the texture is read
            cmd_list->InsertBarrier(tex_reflections, RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_RayTracedShadows(RHI_CommandList* cmd_list)
    {
        // skip when window is minimized or resolution is too small (e.g. during minimize animation)
        const uint32_t min_rt_dimension = 64;
        if (Window::IsMinimized())
            return;

        RHI_Texture* tex_shadows = GetRenderTarget(Renderer_RenderTarget::ray_traced_shadows);

        // skip if render target has invalid or too-small dimensions
        if (tex_shadows && (tex_shadows->GetWidth() < min_rt_dimension || tex_shadows->GetHeight() < min_rt_dimension))
            return;
        
        // clear once if disabled
        static bool cleared = false;
        if (!cvar_ray_traced_shadows.GetValueAs<bool>())
        {
            if (!cleared)
            {
                cmd_list->ClearTexture(tex_shadows, Color::standard_white);
                cleared = true;
            }
            return;
        }
        cleared = false;
        
        // validate ray tracing support
        if (!RHI_Device::IsSupportedRayTracing())
            return;
            
        RHI_AccelerationStructure* tlas = GetTopLevelAccelerationStructure();
        if (!tlas)
            return;
        
        // render
        cmd_list->BeginTimeblock("ray_traced_shadows");
        {
            // create or get sbt (shader binding table) for shadow ray tracing
            RHI_Shader* shader_rgen = GetShader(Renderer_Shader::shadows_ray_generation_r);
            RHI_Shader* shader_miss = GetShader(Renderer_Shader::shadows_ray_miss_r);
            RHI_Shader* shader_hit  = GetShader(Renderer_Shader::shadows_ray_hit_r);
            
            if (!shader_rgen || !shader_miss || !shader_hit)
                return;
            if (!shader_rgen->IsCompiled() || !shader_miss->IsCompiled() || !shader_hit->IsCompiled())
                return;
            
            // set pipeline state for ray tracing
            RHI_PipelineState pso;
            pso.name                    = "ray_traced_shadows";
            pso.shaders[RayGeneration] = shader_rgen;
            pso.shaders[RayMiss]       = shader_miss;
            pso.shaders[RayHit]        = shader_hit;
            cmd_list->SetPipelineState(pso);
            
            // create sbt if needed (once)
            if (!m_std_shadows)
            {
                uint32_t handle_size = RHI_Device::PropertyGetShaderGroupHandleSize();
                m_std_shadows = make_unique<RHI_Buffer>(RHI_Buffer_Type::ShaderBindingTable, handle_size, 3, nullptr, true, "shadows_sbt");
            }
            // update handles every frame in case pipeline changed
            m_std_shadows->UpdateHandles(cmd_list);
            
            // set textures and acceleration structure
            SetCommonTextures(cmd_list);
            cmd_list->SetAccelerationStructure(Renderer_BindingsSrv::tlas, tlas);
            
            // set output texture (as UAV for ray tracing write)
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_shadows, rhi_all_mips, 0, true);
            
            // trace full screen
            uint32_t width  = tex_shadows->GetWidth();
            uint32_t height = tex_shadows->GetHeight();
            cmd_list->TraceRays(width, height, m_std_shadows.get());
            
            // ensure writes complete before the texture is read
            cmd_list->InsertBarrier(tex_shadows, RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::SwapReSTIRReservoirs()
    {
        auto& render_targets = GetRenderTargets();
        swap(render_targets[static_cast<uint8_t>(Renderer_RenderTarget::restir_reservoir0)], 
             render_targets[static_cast<uint8_t>(Renderer_RenderTarget::restir_reservoir_prev0)]);
        swap(render_targets[static_cast<uint8_t>(Renderer_RenderTarget::restir_reservoir1)], 
             render_targets[static_cast<uint8_t>(Renderer_RenderTarget::restir_reservoir_prev1)]);
        swap(render_targets[static_cast<uint8_t>(Renderer_RenderTarget::restir_reservoir2)], 
             render_targets[static_cast<uint8_t>(Renderer_RenderTarget::restir_reservoir_prev2)]);
        swap(render_targets[static_cast<uint8_t>(Renderer_RenderTarget::restir_reservoir3)], 
             render_targets[static_cast<uint8_t>(Renderer_RenderTarget::restir_reservoir_prev3)]);
        swap(render_targets[static_cast<uint8_t>(Renderer_RenderTarget::restir_reservoir4)], 
             render_targets[static_cast<uint8_t>(Renderer_RenderTarget::restir_reservoir_prev4)]);
    }

    void Renderer::Pass_ReSTIR_PathTracing(RHI_CommandList* cmd_list)
    {
        // skip when window is minimized or resolution is too small (e.g. during minimize animation)
        const uint32_t min_rt_dimension = 64;
        if (Window::IsMinimized())
            return;

        RHI_Texture* tex_gi      = GetRenderTarget(Renderer_RenderTarget::restir_output);
        RHI_Texture* reservoir0  = GetRenderTarget(Renderer_RenderTarget::restir_reservoir0);

        // skip if render target has invalid or too-small dimensions
        if (tex_gi && (tex_gi->GetWidth() < min_rt_dimension || tex_gi->GetHeight() < min_rt_dimension))
            return;

        // clear output once when disabled, then skip
        static bool cleared = false;
        if (!cvar_restir_pt.GetValueAs<bool>() || !RHI_Device::IsSupportedRayTracing() || !reservoir0)
        {
            if (!cleared)
            {
                cmd_list->ClearTexture(tex_gi, Color::standard_black);
                cleared = true;
            }
            return;
        }
        cleared = false;
            
        RHI_AccelerationStructure* tlas = GetTopLevelAccelerationStructure();
        if (!tlas)
            return;

        RHI_Texture* reservoir1      = GetRenderTarget(Renderer_RenderTarget::restir_reservoir1);
        RHI_Texture* reservoir2      = GetRenderTarget(Renderer_RenderTarget::restir_reservoir2);
        RHI_Texture* reservoir3      = GetRenderTarget(Renderer_RenderTarget::restir_reservoir3);
        RHI_Texture* reservoir4      = GetRenderTarget(Renderer_RenderTarget::restir_reservoir4);
        RHI_Texture* reservoir_prev0 = GetRenderTarget(Renderer_RenderTarget::restir_reservoir_prev0);
        RHI_Texture* reservoir_prev1 = GetRenderTarget(Renderer_RenderTarget::restir_reservoir_prev1);
        RHI_Texture* reservoir_prev2 = GetRenderTarget(Renderer_RenderTarget::restir_reservoir_prev2);
        RHI_Texture* reservoir_prev3 = GetRenderTarget(Renderer_RenderTarget::restir_reservoir_prev3);
        RHI_Texture* reservoir_prev4 = GetRenderTarget(Renderer_RenderTarget::restir_reservoir_prev4);
        RHI_Texture* tex_skysphere   = GetRenderTarget(Renderer_RenderTarget::skysphere);

        uint32_t width  = tex_gi->GetWidth();
        uint32_t height = tex_gi->GetHeight();

        // initial sampling
        cmd_list->BeginTimeblock("restir_pt_initial");
        {
            RHI_Shader* shader_rgen = GetShader(Renderer_Shader::restir_pt_ray_generation_r);
            RHI_Shader* shader_miss = GetShader(Renderer_Shader::restir_pt_ray_miss_r);
            RHI_Shader* shader_hit  = GetShader(Renderer_Shader::restir_pt_ray_hit_r);
            
            if (!shader_rgen || !shader_miss || !shader_hit)
                return;
            if (!shader_rgen->IsCompiled() || !shader_miss->IsCompiled() || !shader_hit->IsCompiled())
                return;
            
            RHI_PipelineState pso;
            pso.name                   = "restir_pt_initial";
            pso.shaders[RayGeneration] = shader_rgen;
            pso.shaders[RayMiss]       = shader_miss;
            pso.shaders[RayHit]        = shader_hit;
            cmd_list->SetPipelineState(pso);
            
            // sbt
            if (!m_std_restir)
            {
                uint32_t handle_size = RHI_Device::PropertyGetShaderGroupHandleSize();
                m_std_restir = make_unique<RHI_Buffer>(RHI_Buffer_Type::ShaderBindingTable, handle_size, 3, nullptr, true, "restir_sbt");
            }
            m_std_restir->UpdateHandles(cmd_list);
            
            SetCommonTextures(cmd_list);
            cmd_list->SetAccelerationStructure(Renderer_BindingsSrv::tlas, tlas);
            
            tex_skysphere->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_skysphere);
            
            GetBuffer(Renderer_Buffer::GeometryInfo)->ResetOffset();
            cmd_list->SetBuffer(Renderer_BindingsUav::geometry_info, GetBuffer(Renderer_Buffer::GeometryInfo));
            
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_gi, rhi_all_mips, 0, true);
            
            // reservoirs
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir0), reservoir0, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir1), reservoir1, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir2), reservoir2, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir3), reservoir3, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir4), reservoir4, rhi_all_mips, 0, true);
            
            // trace
            cmd_list->TraceRays(width, height, m_std_restir.get());
            
            cmd_list->InsertBarrier(reservoir0, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(reservoir1, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(reservoir2, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(reservoir3, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(reservoir4, RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();

        // temporal resampling
        cmd_list->BeginTimeblock("restir_pt_temporal");
        {
            RHI_Shader* shader_temporal = GetShader(Renderer_Shader::restir_pt_temporal_c);
            if (!shader_temporal || !shader_temporal->IsCompiled())
            {
                cmd_list->EndTimeblock();
                return;
            }
            
            RHI_PipelineState pso;
            pso.name             = "restir_pt_temporal";
            pso.shaders[Compute] = shader_temporal;
            cmd_list->SetPipelineState(pso);
            
            SetCommonTextures(cmd_list);
            
            cmd_list->SetTexture(Renderer_BindingsSrv::reservoir_prev0, reservoir_prev0);
            cmd_list->SetTexture(Renderer_BindingsSrv::reservoir_prev1, reservoir_prev1);
            cmd_list->SetTexture(Renderer_BindingsSrv::reservoir_prev2, reservoir_prev2);
            cmd_list->SetTexture(Renderer_BindingsSrv::reservoir_prev3, reservoir_prev3);
            cmd_list->SetTexture(Renderer_BindingsSrv::reservoir_prev4, reservoir_prev4);
            
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir0), reservoir0, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir1), reservoir1, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir2), reservoir2, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir3), reservoir3, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir4), reservoir4, rhi_all_mips, 0, true);
            
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_gi, rhi_all_mips, 0, true);
            
            const uint32_t thread_group_count_x = 8;
            const uint32_t thread_group_count_y = 8;
            uint32_t dispatch_x = (width + thread_group_count_x - 1) / thread_group_count_x;
            uint32_t dispatch_y = (height + thread_group_count_y - 1) / thread_group_count_y;
            cmd_list->Dispatch(dispatch_x, dispatch_y, 1);
            
            cmd_list->InsertBarrier(reservoir0, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(reservoir1, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(reservoir2, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(reservoir3, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(reservoir4, RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();

        // spatial resampling (using ping-pong buffers to avoid read-write hazard)
        RHI_Texture* reservoir_spatial0 = GetRenderTarget(Renderer_RenderTarget::restir_reservoir_spatial0);
        RHI_Texture* reservoir_spatial1 = GetRenderTarget(Renderer_RenderTarget::restir_reservoir_spatial1);
        RHI_Texture* reservoir_spatial2 = GetRenderTarget(Renderer_RenderTarget::restir_reservoir_spatial2);
        RHI_Texture* reservoir_spatial3 = GetRenderTarget(Renderer_RenderTarget::restir_reservoir_spatial3);
        RHI_Texture* reservoir_spatial4 = GetRenderTarget(Renderer_RenderTarget::restir_reservoir_spatial4);
        
        cmd_list->BeginTimeblock("restir_pt_spatial");
        {
            RHI_Shader* shader_spatial = GetShader(Renderer_Shader::restir_pt_spatial_c);
            if (!shader_spatial || !shader_spatial->IsCompiled())
            {
                cmd_list->EndTimeblock();
                cmd_list->InsertBarrier(tex_gi, RHI_BarrierType::EnsureWriteThenRead);
                return;
            }
            
            RHI_PipelineState pso;
            pso.name             = "restir_pt_spatial";
            pso.shaders[Compute] = shader_spatial;
            cmd_list->SetPipelineState(pso);
            
            SetCommonTextures(cmd_list);
            
            // bind tlas for inline ray tracing visibility checks
            cmd_list->SetAccelerationStructure(Renderer_BindingsSrv::tlas, tlas);
            
            // read from current reservoirs (after temporal pass)
            cmd_list->SetTexture(Renderer_BindingsSrv::reservoir_prev0, reservoir0);
            cmd_list->SetTexture(Renderer_BindingsSrv::reservoir_prev1, reservoir1);
            cmd_list->SetTexture(Renderer_BindingsSrv::reservoir_prev2, reservoir2);
            cmd_list->SetTexture(Renderer_BindingsSrv::reservoir_prev3, reservoir3);
            cmd_list->SetTexture(Renderer_BindingsSrv::reservoir_prev4, reservoir4);
            
            // write to separate spatial buffers (ping-pong to avoid read-write hazard)
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir0), reservoir_spatial0, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir1), reservoir_spatial1, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir2), reservoir_spatial2, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir3), reservoir_spatial3, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir4), reservoir_spatial4, rhi_all_mips, 0, true);
            
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_gi, rhi_all_mips, 0, true);
            
            const uint32_t thread_group_count_x = 8;
            const uint32_t thread_group_count_y = 8;
            uint32_t dispatch_x = (width + thread_group_count_x - 1) / thread_group_count_x;
            uint32_t dispatch_y = (height + thread_group_count_y - 1) / thread_group_count_y;
            cmd_list->Dispatch(dispatch_x, dispatch_y, 1);
            
            // ensure spatial output is complete before copying
            cmd_list->InsertBarrier(reservoir_spatial0, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(reservoir_spatial1, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(reservoir_spatial2, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(reservoir_spatial3, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(reservoir_spatial4, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(tex_gi, RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();
        
        // copy spatial results back to main reservoirs for next frame's temporal pass
        cmd_list->Blit(reservoir_spatial0, reservoir0, false);
        cmd_list->Blit(reservoir_spatial1, reservoir1, false);
        cmd_list->Blit(reservoir_spatial2, reservoir2, false);
        cmd_list->Blit(reservoir_spatial3, reservoir3, false);
        cmd_list->Blit(reservoir_spatial4, reservoir4, false);
        
        SwapReSTIRReservoirs();
        
        // denoise
        Pass_Denoiser(cmd_list, tex_gi, tex_gi);
    }

    void Renderer::Pass_Denoiser(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        return; // todo: fix

        // initialize nrd if not already done
        uint32_t width  = tex_in->GetWidth();
        uint32_t height = tex_in->GetHeight();
        
        if (!RHI_VendorTechnology::NRD_IsAvailable())
        {
            RHI_VendorTechnology::NRD_Initialize(width, height);
        }
        else
        {
            RHI_VendorTechnology::NRD_Resize(width, height);
        }

        if (!RHI_VendorTechnology::NRD_IsAvailable())
        {
            // fallback: just pass through
            if (tex_in && tex_out && tex_in != tex_out)
            {
                cmd_list->Blit(tex_in, tex_out, false);
            }
            return;
        }

        // prepare nrd input textures from g-buffer and path tracer output
        cmd_list->BeginTimeblock("nrd_prepare");
        {
            RHI_Shader* shader = GetShader(Renderer_Shader::nrd_prepare_c);
            if (!shader || !shader->IsCompiled())
            {
                cmd_list->EndTimeblock();
                // fallback
                if (tex_in && tex_out && tex_in != tex_out)
                    cmd_list->Blit(tex_in, tex_out, false);
                return;
            }

            RHI_Texture* nrd_viewz            = GetRenderTarget(Renderer_RenderTarget::nrd_viewz);
            RHI_Texture* nrd_normal_roughness = GetRenderTarget(Renderer_RenderTarget::nrd_normal_roughness);
            RHI_Texture* nrd_diff_radiance    = GetRenderTarget(Renderer_RenderTarget::nrd_diff_radiance_hitdist);
            RHI_Texture* nrd_spec_radiance    = GetRenderTarget(Renderer_RenderTarget::nrd_spec_radiance_hitdist);

            RHI_PipelineState pso;
            pso.name             = "nrd_prepare";
            pso.shaders[Compute] = shader;
            cmd_list->SetPipelineState(pso);

            SetCommonTextures(cmd_list);

            // input: noisy radiance from path tracer
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

            // outputs: nrd input textures
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), nrd_viewz, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), nrd_normal_roughness, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex3), nrd_diff_radiance, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex4), nrd_spec_radiance, rhi_all_mips, 0, true);

            const uint32_t thread_group_count_x = 8;
            const uint32_t thread_group_count_y = 8;
            uint32_t dispatch_x = (width + thread_group_count_x - 1) / thread_group_count_x;
            uint32_t dispatch_y = (height + thread_group_count_y - 1) / thread_group_count_y;
            cmd_list->Dispatch(dispatch_x, dispatch_y, 1);

            // barriers
            cmd_list->InsertBarrier(nrd_viewz, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(nrd_normal_roughness, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(nrd_diff_radiance, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(nrd_spec_radiance, RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();

        // get camera matrices from frame constant buffer
        Matrix view_matrix            = m_cb_frame_cpu.view;
        Matrix projection_matrix      = m_cb_frame_cpu.projection;
        Matrix view_matrix_prev       = m_cb_frame_cpu.view_previous;
        Matrix projection_matrix_prev = m_cb_frame_cpu.projection_previous;

        // get jitter values
        float jitter_x      = m_cb_frame_cpu.taa_jitter_current.x;
        float jitter_y      = m_cb_frame_cpu.taa_jitter_current.y;
        float jitter_prev_x = m_cb_frame_cpu.taa_jitter_previous.x;
        float jitter_prev_y = m_cb_frame_cpu.taa_jitter_previous.y;

        // run nrd denoiser
        RHI_VendorTechnology::NRD_Denoise(
            cmd_list,
            tex_in,
            tex_out,
            view_matrix,
            projection_matrix,
            view_matrix_prev,
            projection_matrix_prev,
            jitter_x,
            jitter_y,
            jitter_prev_x,
            jitter_prev_y,
            m_cb_frame_cpu.delta_time * 1000.0f, // convert to milliseconds
            static_cast<uint32_t>(GetFrameNumber())
        );
    }

    void Renderer::Pass_ScreenSpaceShadows(RHI_CommandList* cmd_list)
    {
        // get resources
        RHI_Texture* tex_sss = GetRenderTarget(Renderer_RenderTarget::sss);

        cmd_list->BeginTimeblock("screen_space_shadows");
        {
            cmd_list->InsertBarrier(tex_sss, RHI_BarrierType::EnsureReadThenWrite); // ensure any previous reads are complete

            // set pipeline state
            RHI_PipelineState pso;
            pso.name             = "screen_space_shadows";
            pso.shaders[Compute] = GetShader(Renderer_Shader::sss_c_bend);
            cmd_list->SetPipelineState(pso);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,     GetRenderTarget(Renderer_RenderTarget::gbuffer_depth)); // read
            cmd_list->SetTexture(Renderer_BindingsUav::tex_sss, tex_sss);                                               // write

            // iterate through all the lights
            static float array_slice_index = 0.0f;
            for (Entity* entity : World::GetEntities())
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
                    light->SetScreenSpaceShadowsSliceIndex(static_cast<uint32_t>(array_slice_index));
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

                    cmd_list->InsertBarrier(tex_sss, RHI_BarrierType::EnsureWriteThenRead); // ensure the texture is ready for the next light
                }
            }

            array_slice_index = 0;
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Skysphere(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_skysphere                    = GetRenderTarget(Renderer_RenderTarget::skysphere);
        RHI_Texture* tex_lut_atmosphere_scatter       = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_scatter);
        RHI_Texture* tex_lut_atmosphere_transmittance = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_transmittance);
        RHI_Texture* tex_lut_atmosphere_multiscatter  = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_multiscatter);
        RHI_Texture* tex_cloud_shape                  = GetRenderTarget(Renderer_RenderTarget::cloud_noise_shape);
        RHI_Texture* tex_cloud_detail                 = GetRenderTarget(Renderer_RenderTarget::cloud_noise_detail);

        cmd_list->BeginTimeblock("skysphere");
        {
            // 1. atmospheric scattering + volumetric clouds
            if (World::GetDirectionalLight())
            {
                RHI_PipelineState pso;
                pso.name             = "skysphere_atmospheric_scattering";
                pso.shaders[Compute] = GetShader(Renderer_Shader::skysphere_c);
                cmd_list->SetPipelineState(pso);
    
                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_skysphere);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_lut_atmosphere_transmittance);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_lut_atmosphere_multiscatter);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex3d, tex_lut_atmosphere_scatter);
                
                // bind 3D cloud noise textures (if available)
                if (tex_cloud_shape)
                    cmd_list->SetTexture(Renderer_BindingsSrv::tex3d_cloud_shape, tex_cloud_shape);
                if (tex_cloud_detail)
                    cmd_list->SetTexture(Renderer_BindingsSrv::tex3d_cloud_detail, tex_cloud_detail);
                
                cmd_list->Dispatch(tex_skysphere);
            }
            else
            {
                cmd_list->ClearTexture(tex_skysphere, Color::standard_black);
            }

            // 2. filter all mip levels
            {
                // filtering can sample from any mip, so we need to generate the mip chain
                Pass_Downscale(cmd_list, tex_skysphere, Renderer_DownsampleFilter::Average);

                RHI_PipelineState pso;
                pso.name             = "skysphere_filter";
                pso.shaders[Compute] = GetShader(Renderer_Shader::light_integration_environment_filter_c);
                cmd_list->SetPipelineState(pso);
            
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_skysphere);
            
                for (uint32_t mip_level = 1; mip_level < tex_skysphere->GetMipCount(); mip_level++)
                {
                    cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_skysphere, mip_level, 1);
            
                    // Set pass constants
                    m_pcb_pass_cpu.set_f3_value(static_cast<float>(mip_level), static_cast<float>(tex_skysphere->GetMipCount()), 0.0f);
                    cmd_list->PushConstants(m_pcb_pass_cpu);
            
                    const uint32_t resolution_x = tex_skysphere->GetWidth() >> mip_level;
                    const uint32_t resolution_y = tex_skysphere->GetHeight() >> mip_level;
                    cmd_list->Dispatch(tex_skysphere);
                    cmd_list->InsertBarrier(tex_skysphere, RHI_BarrierType::EnsureWriteThenRead);
                }
            }
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Light(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        // acquire resources
        RHI_Texture* light_diffuse    = GetRenderTarget(Renderer_RenderTarget::light_diffuse);
        RHI_Texture* light_specular   = GetRenderTarget(Renderer_RenderTarget::light_specular);
        RHI_Texture* light_volumetric = GetRenderTarget(Renderer_RenderTarget::light_volumetric);
    
        // define pipeline state
        RHI_PipelineState pso;
        pso.name             = is_transparent_pass ? "light_transparent" : "light";
        pso.shaders[Compute] = GetShader(Renderer_Shader::light_c);
    
        // dispatch on the bindless light array and the shadow atlas
        cmd_list->BeginTimeblock(pso.name);
        {
            cmd_list->SetPipelineState(pso);
    
            // textures
            SetCommonTextures(cmd_list);
            cmd_list->SetTexture(Renderer_BindingsUav::tex_sss, GetRenderTarget(Renderer_RenderTarget::sss));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,     GetRenderTarget(Renderer_RenderTarget::skysphere));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2,    GetRenderTarget(Renderer_RenderTarget::shadow_atlas));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3,    GetRenderTarget(Renderer_RenderTarget::cloud_shadow));        // cloud shadows
            cmd_list->SetTexture(Renderer_BindingsSrv::tex4,    GetRenderTarget(Renderer_RenderTarget::ray_traced_shadows)); // ray traced shadows
            cmd_list->SetTexture(Renderer_BindingsUav::tex,     light_diffuse);
            cmd_list->SetTexture(Renderer_BindingsUav::tex2,    light_specular);
            cmd_list->SetTexture(Renderer_BindingsUav::tex3,    light_volumetric);
    
            // push constants
            m_pcb_pass_cpu.set_is_transparent_and_material_index(is_transparent_pass);
            m_pcb_pass_cpu.set_f3_value(static_cast<float>(m_count_active_lights), cvar_fog.GetValue());
            cmd_list->PushConstants(m_pcb_pass_cpu);
    
            // dispatch
            cmd_list->Dispatch(light_diffuse, cvar_resolution_scale.GetValue()); // adds read write barrier for light_diffuse internally
            cmd_list->InsertBarrier(light_specular,   RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(light_volumetric, RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();
    }
    
    void Renderer::Pass_Light_Composition(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        // acquire resources
        RHI_Shader* shader_c              = GetShader(Renderer_Shader::light_composition_c);
        RHI_Texture* tex_out              = GetRenderTarget(Renderer_RenderTarget::frame_render);
        RHI_Texture* tex_skysphere        = GetRenderTarget(Renderer_RenderTarget::skysphere);
        RHI_Texture* tex_light_diffuse    = GetRenderTarget(Renderer_RenderTarget::light_diffuse);
        RHI_Texture* tex_light_specular   = GetRenderTarget(Renderer_RenderTarget::light_specular);
        RHI_Texture* tex_light_volumetric = GetRenderTarget(Renderer_RenderTarget::light_volumetric);
        RHI_Texture* tex_gi               = GetRenderTarget(Renderer_RenderTarget::restir_output);

        cmd_list->InsertBarrier(tex_out, RHI_BarrierType::EnsureReadThenWrite);

        cmd_list->BeginTimeblock(is_transparent_pass ? "light_composition_transparent" : "light_composition");
        {
            // set pipeline state
            RHI_PipelineState pso;
            pso.name             = "light_composition";
            pso.shaders[Compute] = shader_c;
            cmd_list->SetPipelineState(pso);

            // push pass constants
            m_pcb_pass_cpu.set_is_transparent_and_material_index(is_transparent_pass);
            m_pcb_pass_cpu.set_f3_value(0.0f, cvar_fog.GetValue(), 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            // set textures
            SetCommonTextures(cmd_list);
            cmd_list->SetTexture(Renderer_BindingsUav::tex,  tex_out);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_skysphere);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_light_diffuse);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex4, tex_light_specular);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex5, tex_light_volumetric);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex6, tex_gi);

            // render
            cmd_list->Dispatch(tex_out, cvar_resolution_scale.GetValue());
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Light_ImageBased(RHI_CommandList* cmd_list)
    {
        // get resources
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
            cmd_list->SetTexture(Renderer_BindingsUav::tex,     tex_out);
            cmd_list->SetTexture(Renderer_BindingsUav::tex_sss, GetRenderTarget(Renderer_RenderTarget::sss));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2,    GetRenderTarget(Renderer_RenderTarget::lut_brdf_specular));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3,    GetRenderTarget(Renderer_RenderTarget::skysphere));

            // set constants
            m_pcb_pass_cpu.set_f3_value(static_cast<float>(GetRenderTarget(Renderer_RenderTarget::skysphere)->GetMipCount()));
            cmd_list->PushConstants(m_pcb_pass_cpu);

            // render
            cmd_list->Dispatch(tex_out, cvar_resolution_scale.GetValue());
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Lut_BrdfSpecular(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_lut_brdf_specular = GetRenderTarget(Renderer_RenderTarget::lut_brdf_specular);

        cmd_list->BeginTimeblock("lut_brdf_specular");
        {
            // set pipeline state
            RHI_PipelineState pso;
            pso.name             = "lut_brdf_specular";
            pso.shaders[Compute] = GetShader(Renderer_Shader::light_integration_brdf_specular_lut_c);
            cmd_list->SetPipelineState(pso);

            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_lut_brdf_specular);
            cmd_list->Dispatch(tex_lut_brdf_specular);

            // for the lifetime of the engine, this will be read as an srv, so transition here
            cmd_list->InsertBarrier(tex_lut_brdf_specular->GetRhiResource(), tex_lut_brdf_specular->GetFormat(), 0, 1, 1, RHI_Image_Layout::Shader_Read);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Lut_AtmosphericScattering(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_lut_atmosphere_scatter      = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_scatter);
        RHI_Texture* tex_lut_atmosphere_transmittance = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_transmittance);
        RHI_Texture* tex_lut_atmosphere_multiscatter  = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_multiscatter);

        cmd_list->BeginTimeblock("lut_atmospheric_scattering");
        {
            // 1. transmittance lut - precomputes optical depth from any point to top of atmosphere
            {
                RHI_PipelineState pso;
                pso.name             = "lut_atmosphere_transmittance";
                pso.shaders[Compute] = GetShader(Renderer_Shader::skysphere_transmittance_lut_c);
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_lut_atmosphere_transmittance);
                cmd_list->Dispatch(tex_lut_atmosphere_transmittance);

                tex_lut_atmosphere_transmittance->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            }

            // 2. multi-scatter lut - approximates infinite bounce scattering (needs transmittance lut)
            {
                RHI_PipelineState pso;
                pso.name             = "lut_atmosphere_multiscatter";
                pso.shaders[Compute] = GetShader(Renderer_Shader::skysphere_multiscatter_lut_c);
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_lut_atmosphere_transmittance);
                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_lut_atmosphere_multiscatter);
                cmd_list->Dispatch(tex_lut_atmosphere_multiscatter);

                tex_lut_atmosphere_multiscatter->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            }

            // 3. legacy 3d lut (backward compatibility)
            {
                RHI_PipelineState pso;
                pso.name             = "lut_atmospheric_scattering";
                pso.shaders[Compute] = GetShader(Renderer_Shader::skysphere_lut_c);
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsUav::tex3d, tex_lut_atmosphere_scatter);
                cmd_list->Dispatch(tex_lut_atmosphere_scatter);

                tex_lut_atmosphere_scatter->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            }
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_CloudNoise(RHI_CommandList* cmd_list)
    {
        // Generate 3D noise textures for volumetric clouds (only once at startup)
        static bool noise_generated = false;
        if (noise_generated)
            return;

        RHI_Texture* tex_shape  = GetRenderTarget(Renderer_RenderTarget::cloud_noise_shape);
        RHI_Texture* tex_detail = GetRenderTarget(Renderer_RenderTarget::cloud_noise_detail);

        if (!tex_shape || !tex_detail)
            return;

        // Check if shaders are ready (they compile async)
        RHI_Shader* shader_shape  = GetShader(Renderer_Shader::cloud_noise_shape_c);
        RHI_Shader* shader_detail = GetShader(Renderer_Shader::cloud_noise_detail_c);
        if (!shader_shape || !shader_shape->IsCompiled() || !shader_detail || !shader_detail->IsCompiled())
            return;

        cmd_list->BeginTimeblock("cloud_noise");
        {
            // Generate shape noise (128x128x128)
            {
                RHI_PipelineState pso;
                pso.name             = "cloud_noise_shape";
                pso.shaders[Compute] = shader_shape;
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsUav::tex3d, tex_shape);
                cmd_list->Dispatch(tex_shape);
            }

            // Generate detail noise (32x32x32)
            {
                RHI_PipelineState pso;
                pso.name             = "cloud_noise_detail";
                pso.shaders[Compute] = shader_detail;
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsUav::tex3d, tex_detail);
                cmd_list->Dispatch(tex_detail);
            }

            // Transition to shader read for later use
            tex_shape->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            tex_detail->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        }
        cmd_list->EndTimeblock();

        noise_generated = true;
    }

    void Renderer::Pass_CloudShadow(RHI_CommandList* cmd_list)
    {
        // skip if clouds are disabled or cloud shadows are off
        if (!cvar_clouds_enabled.GetValueAs<bool>() || cvar_cloud_coverage.GetValue() <= 0.0f || cvar_cloud_shadows.GetValue() <= 0.0f)
            return;

        // Skip if no directional light
        if (!World::GetDirectionalLight())
            return;

        // Check if shader is compiled
        RHI_Shader* shader = GetShader(Renderer_Shader::cloud_shadow_c);
        if (!shader || !shader->IsCompiled())
            return;

        RHI_Texture* tex_shadow = GetRenderTarget(Renderer_RenderTarget::cloud_shadow);
        RHI_Texture* tex_shape  = GetRenderTarget(Renderer_RenderTarget::cloud_noise_shape);
        RHI_Texture* tex_detail = GetRenderTarget(Renderer_RenderTarget::cloud_noise_detail);

        if (!tex_shadow || !tex_shape || !tex_detail)
            return;

        cmd_list->BeginTimeblock("cloud_shadow");
        {
            RHI_PipelineState pso;
            pso.name             = "cloud_shadow";
            pso.shaders[Compute] = shader;
            cmd_list->SetPipelineState(pso);

            // Bind 3D noise textures using the correct slots (t19, t20)
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3d_cloud_shape,  tex_shape);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3d_cloud_detail, tex_detail);

            // Bind shadow map as output
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_shadow);

            cmd_list->Dispatch(tex_shadow);
        }
        cmd_list->EndTimeblock();
    }


    void Renderer::Pass_PostProcess(RHI_CommandList* cmd_list)
    {
        // acquire render targets
        RHI_Texture* rt_frame_output         = GetRenderTarget(Renderer_RenderTarget::frame_output);
        RHI_Texture* rt_frame_output_scratch = GetRenderTarget(Renderer_RenderTarget::frame_output_2);
        cmd_list->BeginMarker("post_process");
    
        // track current input explicitly for robustness
        RHI_Texture* tex_in  = rt_frame_output;
        RHI_Texture* tex_out = rt_frame_output_scratch;
        bool any_pass_ran    = false;
    
        // depth of field
        if (cvar_depth_of_field.GetValueAs<bool>())
        {
            Pass_DepthOfField(cmd_list, tex_in, tex_out);
            swap(tex_in, tex_out);
            any_pass_ran = true;
        }
    
        // motion blur
        if (cvar_motion_blur.GetValueAs<bool>())
        {
            Pass_MotionBlur(cmd_list, tex_in, tex_out);
            swap(tex_in, tex_out);
            any_pass_ran = true;
        }
    
        // bloom
        if (cvar_bloom.GetValueAs<bool>())
        {
            Pass_Bloom(cmd_list, tex_in, tex_out);
            swap(tex_in, tex_out);
            any_pass_ran = true;
        }
    
        // auto-exposure
        if (cvar_auto_exposure_adaptation_speed.GetValue() > 0.0f)
        {
            RHI_Texture* tex_exposure = tex_in;

            // auto-exposure needs mips
            if (any_pass_ran)
            {
                if (!tex_in->HasPerMipViews())
                {
                    tex_exposure = tex_out;
                    cmd_list->Blit(tex_in, tex_exposure, false);
                }
                
                Pass_Downscale(cmd_list, tex_exposure, Renderer_DownsampleFilter::Average);
            }
            
            Pass_AutoExposure(cmd_list, tex_exposure);
        }
    
        // tone-mapping & gamma correction
        Pass_Output(cmd_list, tex_in, tex_out);
        swap(tex_in, tex_out);
    
        // dithering
        if (cvar_dithering.GetValueAs<bool>())
        {
            Pass_Dithering(cmd_list, tex_in, tex_out);
            swap(tex_in, tex_out);
        }
    
        // sharpening
        Renderer_AntiAliasing_Upsampling aa_upsampling = cvar_antialiasing_upsampling.GetValueAs<Renderer_AntiAliasing_Upsampling>();
        bool is_fsr                                    = aa_upsampling == Renderer_AntiAliasing_Upsampling::AA_Fsr_Upscale_Fsr; // fsr does it's own sharpening
        if (cvar_sharpness.GetValueAs<bool>() && !is_fsr)
        {
            Pass_Sharpening(cmd_list, tex_in, tex_out);
            swap(tex_in, tex_out);
        }
    
        // film grain
        if (cvar_film_grain.GetValueAs<bool>())
        {
            Pass_FilmGrain(cmd_list, tex_in, tex_out);
            swap(tex_in, tex_out);
        }
    
        // chromatic aberration
        if (cvar_chromatic_aberration.GetValueAs<bool>())
        {
            Pass_ChromaticAberration(cmd_list, tex_in, tex_out);
            swap(tex_in, tex_out);
        }
    
        // vhs
        if (cvar_vhs.GetValueAs<bool>())
        {
            Pass_Vhs(cmd_list, tex_in, tex_out);
            swap(tex_in, tex_out);
        }
    
        // ensure final output is in rt_frame_output
        if (tex_in != rt_frame_output)
        {
            cmd_list->Copy(tex_in, rt_frame_output, false);
        }
    
        // editor
        Pass_Grid(cmd_list, rt_frame_output);
        Pass_Lines(cmd_list, rt_frame_output);
        Pass_Outline(cmd_list, rt_frame_output);
        Pass_Icons(cmd_list, rt_frame_output);

        cmd_list->EndMarker();
    }

    void Renderer::Pass_Bloom(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // acquire resources
        RHI_Shader* shader_luminance          = GetShader(Renderer_Shader::bloom_luminance_c);
        RHI_Shader* shader_downsample         = GetShader(Renderer_Shader::bloom_downsample_c);
        RHI_Shader* shader_upsample_blend_mip = GetShader(Renderer_Shader::bloom_upsample_blend_mip_c);
        RHI_Shader* shader_blend_frame        = GetShader(Renderer_Shader::bloom_blend_frame_c);
        RHI_Texture* tex_bloom                = GetRenderTarget(Renderer_RenderTarget::bloom);
    
        // calculate the safe mip count
        // we stop when dimensions drop below 32px to avoid the "giant unstable block" artifact
        uint32_t bloom_mip_count = 0;
        for (uint32_t i = 0; i < tex_bloom->GetMipCount(); i++)
        {
            uint32_t mip_width  = tex_bloom->GetWidth() >> i;
            uint32_t mip_height = tex_bloom->GetHeight() >> i;
            
            if (mip_width < 32 || mip_height < 32)
                break;
            
            bloom_mip_count++;
        }
    
        cmd_list->BeginTimeblock("bloom");
    
        // 1. luminance & initial downsample (mip 0)
        // ---------------------------------------------------------
        cmd_list->BeginMarker("luminance");
        {
            // define pipeline state
            RHI_PipelineState pso;
            pso.name             = "bloom_luminance";
            pso.shaders[Compute] = shader_luminance;
            
            // set pipeline state
            cmd_list->SetPipelineState(pso);
    
            // input: hdr frame, output: bloom mip 0
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_bloom, 0, 1);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            
            // render
            cmd_list->Dispatch(tex_bloom->GetWidth(), tex_bloom->GetHeight());
        }
        cmd_list->EndMarker();
    
        // 2. stable downsample chain (mip 0 -> 1 -> ... -> n)
        // ---------------------------------------------------------
        cmd_list->BeginMarker("downsample_chain");
        {
            // define pipeline state
            RHI_PipelineState pso;
            pso.name             = "bloom_downsample";
            pso.shaders[Compute] = shader_downsample;
            
            // set pipeline state
            cmd_list->SetPipelineState(pso);
    
            for (uint32_t i = 0; i < bloom_mip_count - 1; i++)
            {
                // input: mip i, output: mip i+1
                RHI_Texture* input_mip = tex_bloom;
                int input_mip_idx      = i;
                int output_mip_idx     = i + 1;
                
                uint32_t output_width  = tex_bloom->GetWidth() >> output_mip_idx;
                uint32_t output_height = tex_bloom->GetHeight() >> output_mip_idx;
    
                // set textures
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, input_mip, input_mip_idx, 1);
                cmd_list->SetTexture(Renderer_BindingsUav::tex, input_mip, output_mip_idx, 1);
                
                // dispatch
                // calculate groups based on output size, assuming 8x8 thread group
                uint32_t thread_group_count = 8;
                uint32_t dispatch_x = (output_width + thread_group_count - 1) / thread_group_count;
                uint32_t dispatch_y = (output_height + thread_group_count - 1) / thread_group_count;
                
                cmd_list->Dispatch(dispatch_x, dispatch_y);
                
                // barrier to ensure mip i is written before mip i+1 reads it
                cmd_list->InsertBarrier(tex_bloom, RHI_BarrierType::EnsureWriteThenRead);
            }
        }
        cmd_list->EndMarker();
    
        // 3. upsample & blend chain (mip n -> ... -> 1 -> 0)
        // ---------------------------------------------------------
        cmd_list->BeginMarker("upsample_chain");
        {
            // define pipeline state
            RHI_PipelineState pso;
            pso.name             = "bloom_upsample_blend_mip";
            pso.shaders[Compute] = shader_upsample_blend_mip;
            
            // set pipeline state
            cmd_list->SetPipelineState(pso);
    
            // start from the smallest mip we generated and work our way up
            for (int i = bloom_mip_count - 1; i > 0; i--)
            {
                int small_mip_idx = i;
                int big_mip_idx   = i - 1;
                
                // we render into the bigger mip (blending the small onto the big)
                uint32_t big_width  = tex_bloom->GetWidth() >> big_mip_idx;
                uint32_t big_height = tex_bloom->GetHeight() >> big_mip_idx;
    
                // set textures
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_bloom, small_mip_idx, 1); // source (small)
                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_bloom, big_mip_idx, 1);   // target (big)
    
                // dispatch
                uint32_t thread_group_count = 8;
                uint32_t dispatch_x = (big_width + thread_group_count - 1) / thread_group_count;
                uint32_t dispatch_y = (big_height + thread_group_count - 1) / thread_group_count;
                
                cmd_list->Dispatch(dispatch_x, dispatch_y);
                
                // barrier to ensure the blend is finished before the next upsample step reads this mip
                 cmd_list->InsertBarrier(tex_bloom, RHI_BarrierType::EnsureWriteThenRead);
            }
        }
        cmd_list->EndMarker();
    
        // 4. composite (apply to frame)
        // ---------------------------------------------------------
        cmd_list->BeginMarker("blend_with_frame");
        {
            // define pipeline state
            RHI_PipelineState pso;
            pso.name             = "bloom_blend_frame";
            pso.shaders[Compute] = shader_blend_frame;
            
            // set pipeline state
            cmd_list->SetPipelineState(pso);
    
            // set pass constants
            m_pcb_pass_cpu.set_f3_value(cvar_bloom.GetValue(), 0.0f, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);
    
            // set textures
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_bloom, 0, 1); // sample from mip 0
    
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
        m_pcb_pass_cpu.set_f3_value(cvar_tonemapping.GetValue(), cvar_auto_exposure_adaptation_speed.GetValue());
        cmd_list->PushConstants(m_pcb_pass_cpu);

        // set textures
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex2, GetRenderTarget(Renderer_RenderTarget::auto_exposure));

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
        RHI_PipelineState pso;
        pso.name             = "film_grain";
        pso.shaders[Compute] = GetShader(Renderer_Shader::film_grain_c);

        cmd_list->BeginTimeblock(pso.name);
        {
            cmd_list->SetPipelineState(pso);
            m_pcb_pass_cpu.set_f3_value(World::GetCamera()->GetIso(), 0.0f, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            cmd_list->Dispatch(tex_out);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Vhs(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        RHI_PipelineState pso;
        pso.name             = "vhs";
        pso.shaders[Compute] = GetShader(Renderer_Shader::vhs_c);

        cmd_list->BeginTimeblock(pso.name);
        {
            cmd_list->SetPipelineState(pso);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            cmd_list->Dispatch(tex_out);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_AA_Upscale(RHI_CommandList* cmd_list)
    {
        // acquire input
        RHI_Texture* tex_in          = GetRenderTarget(Renderer_RenderTarget::frame_render);
        RHI_Texture* tex_out         = GetRenderTarget(Renderer_RenderTarget::frame_output);
        RHI_Texture* tex_velocity    = GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity);
        RHI_Texture* tex_depth       = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
        const float resolution_scale = cvar_resolution_scale.GetValue();

        cmd_list->BeginTimeblock("aa_upscale");
        {
            // output is displayed in the viewport, so add a barrier to ensure it's not being read by the gpu
            cmd_list->InsertBarrier(tex_out, RHI_BarrierType::EnsureReadThenWrite);
            cmd_list->FlushBarriers();

            Renderer_AntiAliasing_Upsampling method = cvar_antialiasing_upsampling.GetValueAs<Renderer_AntiAliasing_Upsampling>();
            if (method == Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess) // highest quality, most expensive
            {
                RHI_VendorTechnology::XeSS_Dispatch(
                    cmd_list,
                    tex_in,
                    tex_depth,
                    tex_velocity,
                    tex_out
                );
            }
            else if (method == Renderer_AntiAliasing_Upsampling::AA_Fsr_Upscale_Fsr) // high quality, medium expense
            {
                RHI_VendorTechnology::FSR3_Dispatch(
                    cmd_list,
                    World::GetCamera(),
                    m_cb_frame_cpu.delta_time,
                    cvar_sharpness.GetValue(),
                    tex_in,
                    tex_depth,
                    tex_velocity,
                    tex_out
                );
            }
            else if (method == Renderer_AntiAliasing_Upsampling::AA_Fxaa_Upcale_Linear) // low quality, low expense
            {
                Pass_Fxaa(cmd_list, tex_in, tex_out);
                Pass_Fxaa(cmd_list, tex_out, tex_in); // second pass so tex_in holds final result (can't swap due to flag mismatch, doesn't really matter)
                cmd_list->Blit(tex_in, tex_out, false, resolution_scale);
            }
            else // linear upscale, lowest quality, cheapest
            {
                cmd_list->Blit(tex_in, tex_out, false, resolution_scale);
            }

            // wait for vendor tech to finish writing to the texture
            cmd_list->InsertBarrier(tex_out, RHI_BarrierType::EnsureWriteThenRead);

            // used for refraction by the transparent passes, so generate mips to emulate roughness
            Pass_Downscale(cmd_list, tex_out, Renderer_DownsampleFilter::Average);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_AutoExposure(RHI_CommandList* cmd_list, RHI_Texture* tex_in)
    {
        // get resources
        RHI_Texture* tex_exposure          = GetRenderTarget(Renderer_RenderTarget::auto_exposure);
        RHI_Texture* tex_exposure_previous = GetRenderTarget(Renderer_RenderTarget::auto_exposure_previous);

        // define pipeline state
        RHI_PipelineState pso;
        pso.name             = "auto_exposure";
        pso.shaders[Compute] = GetShader(Renderer_Shader::auto_exposure_c);
    
        // do the work
        cmd_list->BeginTimeblock(pso.name);
        {
            cmd_list->SetPipelineState(pso);
    
            // push constants
            m_pcb_pass_cpu.set_f3_value(cvar_auto_exposure_adaptation_speed.GetValue());
            cmd_list->PushConstants(m_pcb_pass_cpu);
    
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);                 // input: current frame
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_exposure_previous); // input: previous exposure value
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_exposure);           // output: current exposure value

            // single dispatch: just writes 1 value
            cmd_list->Dispatch(1, 1, 1);
    
            // copy current into previous for next frame
            cmd_list->Blit(tex_exposure, tex_exposure_previous, false);
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
        shader                 = filter == Renderer_DownsampleFilter::Min ? Renderer_Shader::ffx_spd_min_c: shader;
        shader                 = filter == Renderer_DownsampleFilter::Max ? Renderer_Shader::ffx_spd_max_c: shader;
        RHI_Shader* shader_c   = GetShader(shader);

        cmd_list->BeginMarker("downscale");
        {
            cmd_list->InsertBarrier(GetBuffer(Renderer_Buffer::SpdCounter));

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
            cmd_list->InsertBarrier(tex, RHI_BarrierType::EnsureWriteThenRead);
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
            m_pcb_pass_cpu.set_f3_value(cvar_sharpness.GetValue(), 0.0f, 0.0f);
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
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, GetStandardTexture(Renderer_StandardTexture::Noise_blue));
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);

            // render
            cmd_list->Dispatch(tex_out);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Blur(RHI_CommandList* cmd_list, RHI_Texture* tex_in, const bool bilateral, const float radius, const uint32_t mip /*= rhi_all_mips*/)
    {
        // acquire shader
        RHI_Shader* shader_c = GetShader(bilateral ? Renderer_Shader::blur_gaussian_bilaterial_c : Renderer_Shader::blur_gaussian_c);

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
            Vector3 pos_camera = World::GetCamera() ? World::GetCamera()->GetEntity()->GetPosition() : Vector3::Zero;
            for (Entity* entity : World::GetEntities())
            {
                // icons will eratically move all over the screen if they are close the camera
                // that's because their direction can change very fast, so we skip those
                if ((entity->GetPosition() - pos_camera).LengthSquared() <= 0.01f)
                    continue;

                if (entity->GetComponent<AudioSource>())
                {
                    if (cvar_audio_sources.GetValueAs<bool>())
                    {
                        m_icons.emplace_back(make_tuple(GetStandardTexture(Renderer_StandardTexture::Gizmo_audio_source), entity->GetPosition()));
                    }
                }
                else if (Light* light = entity->GetComponent<Light>())
                {
                    if (cvar_lights.GetValueAs<bool>())
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
                    cmd_list->InsertBarrier(tex_out, RHI_BarrierType::EnsureWriteThenRead);
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
        if (!cvar_grid.GetValueAs<bool>())
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
        pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output);
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
            pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output);
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
        if (!cvar_selection_outline.GetValueAs<bool>() || Engine::IsFlagSet(EngineMode::Playing))
            return;

        // acquire shaders
        RHI_Shader* shader_v = GetShader(Renderer_Shader::outline_v);
        RHI_Shader* shader_p = GetShader(Renderer_Shader::outline_p);
        RHI_Shader* shader_c = GetShader(Renderer_Shader::outline_c);

        if (Camera* camera = World::GetCamera())
        {
            const std::vector<Entity*>& selected_entities = camera->GetSelectedEntities();
            if (!selected_entities.empty())
            {
                cmd_list->BeginTimeblock("outline");
                {
                    RHI_Texture* tex_outline = GetRenderTarget(Renderer_RenderTarget::outline);

                    // draw silhouettes for all selected entities
                    bool any_rendered = false;
                    cmd_list->BeginMarker("color_silhouette");
                    {
                        // set pipeline state once for all entities
                        RHI_PipelineState pso;
                        pso.name                             = "color_silhouette";
                        pso.shaders[RHI_Shader_Type::Vertex] = shader_v;
                        pso.shaders[RHI_Shader_Type::Pixel]  = shader_p;
                        pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Solid);
                        pso.blend_state                      = GetBlendState(Renderer_BlendState::Additive);
                        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::Off);
                        pso.render_target_color_textures[0]  = tex_outline;
                        pso.clear_color[0]                   = Color::standard_transparent;
                        cmd_list->SetPipelineState(pso);
                    
                        // render each selected entity
                        for (Entity* entity_selected : selected_entities)
                        {
                            if (!entity_selected)
                                continue;
                                
                            Renderable* renderable = entity_selected->GetComponent<Renderable>();
                            if (!renderable)
                                continue;
                                
                            // no mesh (vertex/index buffer) can occur if the mesh is selected but not loaded or the user removed it
                            if (!renderable->GetVertexBuffer() || !renderable->GetIndexBuffer())
                                continue;

                            // push draw data
                            m_pcb_pass_cpu.set_f4_value(Color::standard_renderer_lines);
                            m_pcb_pass_cpu.transform = entity_selected->GetMatrix();
                            cmd_list->PushConstants(m_pcb_pass_cpu);

                            cmd_list->SetBufferVertex(renderable->GetVertexBuffer());
                            cmd_list->SetBufferIndex(renderable->GetIndexBuffer());
                            cmd_list->DrawIndexed(renderable->GetIndexCount(), renderable->GetIndexOffset(), renderable->GetVertexOffset());
                            any_rendered = true;
                        }
                    }
                    cmd_list->EndMarker();
                    
                    if (any_rendered)
                    {
                        // blur the color silhouette
                        {
                            const float radius = 30.0f;
                            Pass_Blur(cmd_list, tex_outline, false, radius);
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
        const bool draw       = cvar_performance_metrics.GetValueAs<bool>();
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
