#/*
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

//= INCLUDES ===========================
#include "pch.h"
#include "Renderer.h"
#include "bend_sss_cpu.h"
#include "../Display/Display.h"
#include "../Profiling/Profiler.h"
#include "../World/Entity.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_FidelityFX.h"
//======================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        namespace visibility
        {
            unordered_map<uint64_t, float> distances_squared;
            unordered_map<uint64_t, Rectangle> rectangles;
            unordered_map<uint64_t, BoundingBox> boxes;

            void clear()
            {
                distances_squared.clear();
                rectangles.clear();
                boxes.clear();
            }

            void frustum_culling_and_depth_sorting(Camera* camera, unordered_map<Renderer_Entity, vector<shared_ptr<Entity>>>& renderables_)
            {
                auto sort_renderables = [&camera, &renderables_](Renderer_Entity entity_type, const bool are_transparent)
                {
                    vector<shared_ptr<Entity>>& renderables = renderables_[entity_type];

                    // frustum culling
                    for (shared_ptr<Entity>& entity : renderables)
                    {
                        shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>();

                        renderable->SetFlag(RenderableFlags::OccludedCpu, !camera->IsInViewFrustum(renderable));
                        renderable->SetFlag(RenderableFlags::Occluder,    false);
                    }

                    // depth sorting
                    {
                        Vector3 camera_position = camera->GetEntity()->GetPosition();

                        auto squared_distance = [&camera_position](const shared_ptr<Entity>& entity)
                        {
                            uint64_t entity_id = entity->GetObjectId();

                            auto it = distances_squared.find(entity_id);
                            if (it != distances_squared.end())
                            {
                                return it->second;
                            }
                            else
                            {
                                shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>();
                                BoundingBoxType type              = renderable->HasInstancing() ? BoundingBoxType::TransformedInstances : BoundingBoxType::Transformed;
                                Vector3 position                  = renderable->GetBoundingBox(type).GetCenter();
                                float distance_squared            = (position - camera_position).LengthSquared();
                                distances_squared[entity_id]      = distance_squared;

                                return distance_squared;
                            }
                        };

                        sort(renderables.begin(), renderables.end(), [&squared_distance, &are_transparent](const shared_ptr<Entity>& a, const shared_ptr<Entity>& b)
                        {
                            // skip entities which are outside of the view frustum
                            if (a->GetComponent<Renderable>()->HasFlag(OccludedCpu) || b->GetComponent<Renderable>()->HasFlag(OccludedCpu))
                                return false;

                            if (are_transparent)
                            {
                                // back-to-front for transparent
                                return squared_distance(a) > squared_distance(b);
                            }
                            else
                            {
                                // front-to-back for opaque
                                return squared_distance(a) < squared_distance(b);
                            }
                        });
                    }
                };

                sort_renderables(Renderer_Entity::Geometry, false);
                sort_renderables(Renderer_Entity::GeometryInstanced, false);
                sort_renderables(Renderer_Entity::GeometryTransparent, true);
                sort_renderables(Renderer_Entity::GeometryTransparentInstanced, true);
            }

            void determine_occluders(Camera* camera, unordered_map<Renderer_Entity, vector<shared_ptr<Entity>>>& renderables)
            {
                bool is_transparent_pass = false;
                uint32_t start_index     = !is_transparent_pass ? 0 : 2;
                uint32_t end_index       = !is_transparent_pass ? 2 : 4;
                uint32_t occluder_count  = 0;
                for (uint32_t i = start_index; i < end_index; i++)
                {
                    auto& entities = renderables[static_cast<Renderer_Entity>(i)];
                    if (entities.empty())
                        continue;

                    for (uint32_t index_entity = 0; index_entity < entities.size(); index_entity++)
                    {
                        shared_ptr<Entity>& entity        = entities[index_entity];
                        shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>();
                        if (!renderable || !renderable->ReadyToRender() || renderable->HasFlag(RenderableFlags::OccludedCpu))
                            continue;

                        // compute screen space rectangle
                        BoundingBoxType box_type          = renderable->HasInstancing() ? BoundingBoxType::TransformedInstances : BoundingBoxType::Transformed;
                        BoundingBox box                   = renderable->GetBoundingBox(box_type);
                        Rectangle rectangle               = camera->WorldToScreenCoordinates(box);
                        boxes[entity->GetObjectId()]      = box;
                        rectangles[entity->GetObjectId()] = rectangle;

                        bool factor_screen_size = rectangle.Area() >= 65536.0f;
                        bool factor_inside      = box.Contains(camera->GetEntity()->GetPosition()); // say we are in a building
                        bool factor_count       = occluder_count < 32;
                        if (factor_count && factor_screen_size && !factor_inside)
                        {
                            renderable->SetFlag(RenderableFlags::Occluder, true);
                            occluder_count++;
                        }
                    }
                }
            }

            void remove_false_gpu_occlusion(shared_ptr<Entity>& entity_occludee, vector<shared_ptr<Entity>>& entities)
            {
                // if this entity is outside of the view frustum, don't bother
                shared_ptr<Renderable> renderable_occludee = entity_occludee->GetComponent<Renderable>();
                if (!renderable_occludee || renderable_occludee->HasFlag(OccludedCpu))
                    return;

                bool is_visible         = true;
                uint32_t occluder_count = 0;
                for (shared_ptr<Entity>& entity_occluder : entities)
                {
                    shared_ptr<Renderable> renderable_occluder = entity_occluder->GetComponent<Renderable>();
                    if (!renderable_occluder || entity_occludee->GetObjectId() == entity_occluder->GetObjectId())
                        continue;

                    if (renderable_occluder->HasFlag(Occluder))
                    {
                        // project world space axis-aligned bounding boxes into screen space
                        Rectangle& rectangle_occludee = rectangles[entity_occludee->GetObjectId()];
                        Rectangle& rectangle_occluder = rectangles[entity_occluder->GetObjectId()];

                        // if it's contained by at least one occluder, it's not visible
                        if (rectangle_occluder.Contains(rectangle_occludee))
                        {
                            is_visible = false;
                            break;
                        }

                        occluder_count++;
                    }

                    // use the 4 first (and front most) occluders
                    if (occluder_count > 4)
                        break;
                }

                if (is_visible)
                {
                    renderable_occludee->SetFlag(RenderableFlags::OccludedGpu, false);
                }
            }

            void get_gpu_occlusion_query_results(RHI_CommandList* cmd_list, unordered_map<Renderer_Entity, vector<shared_ptr<Entity>>>& renderables)
            {
                cmd_list->UpdateOcclusionQueries();

                bool is_transparent_pass = false;
                uint32_t start_index     = !is_transparent_pass ? 0 : 2;
                uint32_t end_index       = !is_transparent_pass ? 2 : 4;
                for (uint32_t i = start_index; i < end_index; i++)
                {
                    auto& entities = renderables[static_cast<Renderer_Entity>(i)];
                    if (entities.empty())
                        continue;

                    for (shared_ptr<Entity>& entity : entities)
                    {
                        shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>();
                        if (!renderable)
                            continue;

                        bool occluded = cmd_list->GetOcclusionQueryResult(entity->GetObjectId());
                        renderable->SetFlag(RenderableFlags::OccludedGpu, occluded);

                        // counter occlusion query latency by removing false gpu occlusions
                        if (occluded)
                        {
                            remove_false_gpu_occlusion(entity, entities);
                        }
                    }
                }
            }
        }

        mutex mutex_generate_mips;
        bool light_integration_brdf_speculat_lut_completed = false;
        const float thread_group_count                     = 8.0f;

        #define thread_group_count_x(tex) static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex->GetWidth())  / thread_group_count))
        #define thread_group_count_y(tex) static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex->GetHeight()) / thread_group_count))

        // called by: Pass_ShadowMaps(), Pass_Depth_Prepass(), Pass_GBuffer()
        void draw_renderable(RHI_CommandList* cmd_list, RHI_PipelineState& pso, Camera* camera, Renderable* renderable, Light* light = nullptr, uint32_t array_index = 0)
        {
            uint32_t instance_start_index = 0;
            bool draw_instanced           = pso.instancing && renderable->HasInstancing();

            if (draw_instanced)
            {
                for (uint32_t group_index = 0; group_index < renderable->GetInstancePartitionCount(); group_index++)
                {
                    uint32_t group_end_index = renderable->GetBoundingBoxGroupEndIndices()[group_index];
                    uint32_t instance_count  = group_end_index - instance_start_index;

                    // skip instance groups outside of the view frustum
                    {
                        const BoundingBox& bounding_box_group = renderable->GetBoundingBox(BoundingBoxType::TransformedInstanceGroup, group_index);

                        if (light)
                        {
                            if (!light->IsInViewFrustum(renderable, array_index))
                            {
                                instance_start_index = group_end_index;
                                continue;
                            }
                        }
                        else if (!camera->IsInViewFrustum(bounding_box_group))
                        {
                            instance_start_index = group_end_index;
                            continue;
                        }
                    }

                    // skip this iteration if we've reached the total number of instances
                    if (instance_start_index + instance_count >= renderable->GetInstanceCount())
                        continue;

                    if (instance_count > 0)
                    {
                        cmd_list->DrawIndexed(
                            renderable->GetIndexCount(),
                            renderable->GetIndexOffset(),
                            renderable->GetVertexOffset(),
                            instance_start_index,
                            instance_count
                        );
                    }

                    instance_start_index = group_end_index;
                }
            }
            else 
            {
                cmd_list->DrawIndexed(
                    renderable->GetIndexCount(),
                    renderable->GetIndexOffset(),
                    renderable->GetVertexOffset()
                );
            }
        }
    }

    void Renderer::SetStandardResources(RHI_CommandList* cmd_list)
    {
        // these will only bind if needed

        // constant buffers
        cmd_list->SetConstantBuffer(Renderer_BindingsCb::frame, GetConstantBufferFrame());

        // structure buffers
        cmd_list->SetStructuredBuffer(Renderer_BindingsUav::sb_materials, GetStructuredBuffer(Renderer_StructuredBuffer::Materials));
        cmd_list->SetStructuredBuffer(Renderer_BindingsUav::sb_lights,    GetStructuredBuffer(Renderer_StructuredBuffer::Lights));
        cmd_list->SetStructuredBuffer(Renderer_BindingsUav::sb_spd,       GetStructuredBuffer(Renderer_StructuredBuffer::Spd));

        // textures - todo: could at these two in the bindless array
        cmd_list->SetTexture(Renderer_BindingsSrv::noise_normal, GetStandardTexture(Renderer_StandardTexture::Noise_normal));
        cmd_list->SetTexture(Renderer_BindingsSrv::noise_blue,   GetStandardTexture(Renderer_StandardTexture::Noise_blue));
    }

    void Renderer::ProduceFrame(RHI_CommandList* cmd_list)
    {
        SP_PROFILE_CPU();

        // acquire render targets
        RHI_Texture* rt_render   = GetRenderTarget(Renderer_RenderTarget::frame_render).get();
        RHI_Texture* rt_render_2 = GetRenderTarget(Renderer_RenderTarget::frame_render_2).get();
        RHI_Texture* rt_output   = GetRenderTarget(Renderer_RenderTarget::frame_output).get();

        UpdateConstantBufferFrame(cmd_list);

        Pass_VariableRateShading(cmd_list);
        Pass_Skysphere(cmd_list);

        // light integration
        {
            if (!light_integration_brdf_speculat_lut_completed)
            {
                Pass_Light_Integration_BrdfSpecularLut(cmd_list);
            }

            if (m_environment_mips_to_filter_count > 0)
            {
                Pass_Light_Integration_EnvironmentPrefilter(cmd_list);
            }
        }

        if (shared_ptr<Camera> camera = GetCamera())
        { 
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
 
            // opaque
            {
                Pass_Visibility(cmd_list);
                Pass_Depth_Prepass(cmd_list, false);
                Pass_GBuffer(cmd_list);
                Pass_Ssgi(cmd_list);
                Pass_Ssr(cmd_list, rt_render);
                Pass_Sss(cmd_list);
                Pass_Light(cmd_list);                        // compute diffuse and specular buffers
                Pass_Light_Composition(cmd_list, rt_render); // compose diffuse, specular, ssgi, volumetric etc.
                Pass_Light_ImageBased(cmd_list, rt_render);  // apply IBL and SSR
            
                cmd_list->Blit(rt_render, GetRenderTarget(Renderer_RenderTarget::frame_render_opaque).get(), false);
            }
            
            // transparent
            if (do_transparent_pass) // actual geometry processing
            {
                // blit the frame so that refraction can sample from it
                cmd_list->Copy(rt_render, rt_render_2, true);
            
                // generate frame mips so that the reflections can simulate roughness
                Pass_Ffx_Spd(cmd_list, rt_render_2, Renderer_DownsampleFilter::Average);
            
                // blur the smaller mips to reduce blockiness/flickering
                for (uint32_t i = 1; i < rt_render_2->GetMipCount(); i++)
                {
                    const float radius = 1.0f;
                    Pass_Blur_Gaussian(cmd_list, rt_render_2, nullptr, Renderer_Shader::blur_gaussian_c, radius, i);
                }
            
                Pass_Depth_Prepass(cmd_list, do_transparent_pass);
                Pass_GBuffer(cmd_list, do_transparent_pass);
                Pass_Ssr(cmd_list, rt_render, do_transparent_pass);
                Pass_Light(cmd_list, do_transparent_pass);
                Pass_Light_Composition(cmd_list, rt_render, do_transparent_pass);
                Pass_Light_ImageBased(cmd_list, rt_render, do_transparent_pass);
            }

            Pass_PostProcess(cmd_list);
            Pass_Grid(cmd_list, rt_output);
            Pass_Lines(cmd_list, rt_output);
            Pass_Outline(cmd_list, rt_output);
            Pass_Icons(cmd_list, rt_output);
        }
        else
        {
            GetCmdList()->ClearRenderTarget(rt_output, Color::standard_black);
        }

        Pass_Text(cmd_list, rt_output);

        // transition the render target to a readable state so it can be rendered
        // within the viewport or copied to the swap chain back buffer
        rt_output->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
    }

    void Renderer::Pass_VariableRateShading(RHI_CommandList* cmd_list)
    {
        if (!GetOption<bool>(Renderer_Option::VariableRateShading))
            return;

        // acquire shader
        RHI_Shader* shader_c = GetShader(Renderer_Shader::variable_rate_shading_c).get();
        if (!shader_c->IsCompiled())
            return;

        // acquire textures
        RHI_Texture* tex_in  = GetFrameTexture();
        RHI_Texture* tex_out = GetRenderTarget(Renderer_RenderTarget::shading_rate).get();

        cmd_list->BeginTimeblock("variable_rate_shading");

        // set pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_pcb_pass_cpu.set_resolution_out(tex_out);
        PushPassConstants(cmd_list);

        // set textures
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
        cmd_list->SetTexture(Renderer_BindingsUav::tex_uint, tex_out);

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_ShadowMaps(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        // all entities are rendered from the lights point of view
        // opaque entities write their depth information to a depth buffer, using just a vertex shader
        // transparent objects read the opaque depth but don't write their own, instead, they write their color information using a pixel shader

        // acquire shaders
        RHI_Shader* shader_v           = GetShader(Renderer_Shader::depth_light_v).get();
        RHI_Shader* shader_instanced_v = GetShader(Renderer_Shader::depth_light_instanced_v).get();
        RHI_Shader* shader_p           = GetShader(Renderer_Shader::depth_light_p).get();
        if (!shader_v->IsCompiled() || !shader_instanced_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        lock_guard lock(m_mutex_renderables);
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
                if (!light->IsFlagSet(LightFlags::Shadows) || light->GetIntensityWatt(GetCamera().get()) == 0.0f)
                    continue;

                // skip lights that don't cast transparent shadows (if this is a transparent pass)
                if (is_transparent_pass && !light->IsFlagSet(LightFlags::ShadowsTransparent))
                    continue;

                // set pipeline state
                static RHI_PipelineState pso;
                pso.instancing                      = i == 1 || i == 3;
                pso.shader_vertex                   = !pso.instancing ? shader_v : shader_instanced_v;
                pso.shader_pixel                    = shader_p;
                pso.blend_state                     = is_transparent_pass ? GetBlendState(Renderer_BlendState::Alpha).get() : GetBlendState(Renderer_BlendState::Disabled).get();
                pso.depth_stencil_state             = is_transparent_pass ? GetDepthStencilState(Renderer_DepthStencilState::Depth_read).get() : GetDepthStencilState(Renderer_DepthStencilState::Depth_read_write_stencil_read).get();
                pso.render_target_color_textures[0] = light->GetColorTexture();
                pso.render_target_depth_texture     = light->GetDepthTexture();
                pso.name                            = "shadow_maps";

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
                        // disable depth clipping so that we can capture silhouettes even behind the light
                        pso.rasterizer_state = GetRasterizerState(Renderer_RasterizerState::Light_directional).get();
                    }
                    else
                    {
                        pso.rasterizer_state = GetRasterizerState(Renderer_RasterizerState::Light_point_spot).get();
                    }

                    // start pso
                    cmd_list->SetPipelineState(pso);

                    // go through all of the entities
                    for (shared_ptr<Entity>& entity : entities)
                    {
                        // acquire renderable component
                        shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>();
                        if (!renderable || !renderable->ReadyToRender() || !renderable->HasFlag(RenderableFlags::CastsShadows))
                            continue;

                        // skip objects outside of the view frustum
                        if (!light->IsInViewFrustum(renderable.get(), array_index))
                            continue;

                        cmd_list->SetCullMode(static_cast<RHI_CullMode>(renderable->GetMaterial()->GetProperty(MaterialProperty::CullMode)));

                        // set vertex, index and instance buffers
                        {
                            cmd_list->SetBufferVertex(renderable->GetVertexBuffer());
                            if (pso.instancing)
                            {
                                cmd_list->SetBufferVertex(renderable->GetInstanceBuffer(), 1);
                            }

                            cmd_list->SetBufferIndex(renderable->GetIndexBuffer());
                        }

                        // set pass constants
                        {
                            // for the vertex shader
                            m_pcb_pass_cpu.set_f3_value2(static_cast<float>(array_index), static_cast<float>(light->GetIndex()), 0.0f);
                            m_pcb_pass_cpu.transform = entity->GetMatrix();

                            // for the pixel shader
                            if (Material* material = renderable->GetMaterial())
                            {
                                m_pcb_pass_cpu.set_f3_value(
                                    material->HasTexture(MaterialTexture::AlphaMask) ? 1.0f : 0.0f,
                                    material->HasTexture(MaterialTexture::Color)     ? 1.0f : 0.0f
                                );

                                m_pcb_pass_cpu.set_is_transparent_and_material_index(is_transparent_pass, material->GetIndex());
                            }

                            PushPassConstants(cmd_list);
                        }

                        draw_renderable(cmd_list, pso, GetCamera().get(), renderable.get(), light.get(), array_index);
                    }
                }
            }
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Visibility(RHI_CommandList* cmd_list)
    {
        // cpu pass
        // forest time: 0.05 ms

        cmd_list->BeginTimeblock("visibility", false, false);

        visibility::clear();
        visibility::frustum_culling_and_depth_sorting(m_camera.get(), m_renderables);
        visibility::determine_occluders(m_camera.get(), m_renderables);

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Depth_Prepass(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        // acquire shaders
        RHI_Shader* shader_v           = GetShader(Renderer_Shader::depth_prepass_v).get();
        RHI_Shader* shader_instanced_v = GetShader(Renderer_Shader::depth_prepass_instanced_v).get();
        RHI_Shader* shader_p           = GetShader(Renderer_Shader::depth_prepass_alpha_test_p).get();
        if (!shader_v->IsCompiled() || !shader_instanced_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        RHI_Texture* tex_depth = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth).get();
        bool vrs               = GetOption<bool>(Renderer_Option::VariableRateShading);

        lock_guard lock(m_mutex_renderables);
        cmd_list->BeginTimeblock(!is_transparent_pass ? "depth_prepass" : "depth_prepass_transparent");

        if (!is_transparent_pass)
        {
            // clear the first time
            cmd_list->ClearRenderTarget(GetRenderTarget(Renderer_RenderTarget::gbuffer_depth).get(), rhi_color_dont_care, 0.0f);
        }

        auto pass = [cmd_list, shader_v, shader_instanced_v, shader_p, tex_depth, vrs](bool is_transparent_pass, bool is_occluder_pass)
        {
            uint32_t start_index = !is_transparent_pass ? 0 : 2;
            uint32_t end_index   = !is_transparent_pass ? 2 : 4;
            bool is_first_pass   = true;
            for (uint32_t i = start_index; i < end_index; i++)
            {
                // acquire entities
                auto& entities = m_renderables[static_cast<Renderer_Entity>(i)];
                if (entities.empty())
                    continue;

                // set pipeline state
                static RHI_PipelineState pso;
                pso.name                        = !is_transparent_pass ? "depth_prepass" : "depth_prepass_transparent";
                pso.instancing                  = i == 1 || i == 3;
                pso.shader_vertex               = !pso.instancing ? shader_v : shader_instanced_v;
                pso.shader_pixel                = pso.instancing ? shader_p : nullptr; // alpha testing - instanced geometry is vegetation which needs alpha testing (not an ideal way to detect this)
                pso.rasterizer_state            = GetRasterizerState(Renderer_RasterizerState::Solid_cull_back).get();
                pso.blend_state                 = GetBlendState(Renderer_BlendState::Disabled).get();
                pso.depth_stencil_state         = GetDepthStencilState(Renderer_DepthStencilState::Depth_read_write_stencil_read).get();
                pso.render_target_depth_texture = tex_depth;
                pso.clear_depth                 = rhi_depth_load;
                pso.render_target_vrs           = vrs ? GetRenderTarget(Renderer_RenderTarget::shading_rate).get() : nullptr;
                cmd_list->SetPipelineState(pso);

                for (shared_ptr<Entity>& entity : entities)
                {
                    shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>();
                    if (!renderable || !renderable->ReadyToRender() || renderable->HasFlag(RenderableFlags::OccludedCpu))
                        continue;

                    if (!is_transparent_pass)
                    {
                        bool render = true;
                        if (is_occluder_pass)
                        {
                            render &= renderable->HasFlag(RenderableFlags::Occluder);
                        }
                        else // occludee
                        {
                            render &= !renderable->HasFlag(RenderableFlags::Occluder);
                        }

                        if (!render)
                            continue;
                    }

                    cmd_list->SetCullMode(static_cast<RHI_CullMode>(renderable->GetMaterial()->GetProperty(MaterialProperty::CullMode)));

                    // set vertex, index and instance buffers
                    {
                        cmd_list->SetBufferVertex(renderable->GetVertexBuffer());
                        if (pso.instancing)
                        {
                            cmd_list->SetBufferVertex(renderable->GetInstanceBuffer(), 1);
                        }

                        cmd_list->SetBufferIndex(renderable->GetIndexBuffer());
                    }

                    // set pass constants
                    {
                        if (Material* material = renderable->GetMaterial())
                        {
                            // for alpha testing
                            m_pcb_pass_cpu.set_f3_value(
                                material->HasTexture(MaterialTexture::AlphaMask) ? 1.0f : 0.0f,
                                material->HasTexture(MaterialTexture::Color)     ? 1.0f : 0.0f,
                                material->GetProperty(MaterialProperty::ColorA)
                            );

                            m_pcb_pass_cpu.set_is_transparent_and_material_index(is_transparent_pass, material->GetIndex());
                        }

                        m_pcb_pass_cpu.transform = entity->GetMatrix();
                        PushPassConstants(cmd_list);
                    }

                    if (!is_transparent_pass)
                    {
                        cmd_list->BeginOcclusionQuery(entity->GetObjectId());
                    }

                    draw_renderable(cmd_list, pso, GetCamera().get(), renderable.get());

                    if (!is_transparent_pass)
                    {
                        cmd_list->EndOcclusionQuery();
                    }
                }
            }
        };

        if (!is_transparent_pass) // opaque
        {
            pass(false, true);  // occluders
            pass(false, false); // occludees (and occlusion queries)

            visibility::get_gpu_occlusion_query_results(cmd_list, m_renderables);

            cmd_list->Blit(
                tex_depth,
                GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque).get(),
                false
            );
        }
        else // transparent
        {
            pass(true, false);
        }

        cmd_list->Blit(
            tex_depth,
            GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_output).get(),
            false
        );

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

        // acquire render targets
        RHI_Texture* tex_color    = GetRenderTarget(Renderer_RenderTarget::gbuffer_color).get();
        RHI_Texture* tex_normal   = GetRenderTarget(Renderer_RenderTarget::gbuffer_normal).get();
        RHI_Texture* tex_material = GetRenderTarget(Renderer_RenderTarget::gbuffer_material).get();
        RHI_Texture* tex_velocity = GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity).get();
        RHI_Texture* tex_depth    = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth).get();

        lock_guard lock(m_mutex_renderables);
        cmd_list->BeginTimeblock(is_transparent_pass ? "g_buffer_transparent" : "g_buffer");

        // deduce some things
        bool wireframe                        = GetOption<bool>(Renderer_Option::Debug_Wireframe);
        RHI_RasterizerState* rasterizer_state = GetRasterizerState(Renderer_RasterizerState::Solid_cull_back).get();
        rasterizer_state                      = wireframe ? GetRasterizerState(Renderer_RasterizerState::Wireframe_cull_none).get() : rasterizer_state;
        bool vrs                              = GetOption<bool>(Renderer_Option::VariableRateShading);

        uint32_t start_index = !is_transparent_pass ? 0 : 2;
        uint32_t end_index   = !is_transparent_pass ? 2 : 4;
        bool is_first_pass   = true;
        for (uint32_t i = start_index; i < end_index; i++)
        {
            // acquire entities
            auto& entities = m_renderables[static_cast<Renderer_Entity>(i)];
            if (entities.empty())
                continue;

            // note: if is_transparent_pass is true we could simply clear the RTs, however we don't do this as fsr
            // can be enabled, and if it is, it will expect the RTs to contain both the opaque and transparent data

            // set pipeline state
            RHI_PipelineState pso;
            pso.name                            = is_transparent_pass ? "g_buffer_transparent" : "g_buffer";
            pso.instancing                      = i == 1 || i == 3;
            pso.shader_pixel                    = shader_p;
            pso.shader_vertex                   = pso.instancing ? shader_v_instanced : shader_v;
            pso.blend_state                     = GetBlendState(Renderer_BlendState::Disabled).get();
            pso.rasterizer_state                = rasterizer_state;
            pso.depth_stencil_state             = GetDepthStencilState(Renderer_DepthStencilState::Depth_read).get();
            pso.render_target_color_textures[0] = tex_color;
            pso.clear_color[0]                  = (!is_first_pass || pso.instancing || is_transparent_pass) ? rhi_color_load : Color::standard_transparent;
            pso.render_target_color_textures[1] = tex_normal;
            pso.clear_color[1]                  = pso.clear_color[0];
            pso.render_target_color_textures[2] = tex_material;
            pso.clear_color[2]                  = pso.clear_color[0];
            pso.render_target_color_textures[3] = tex_velocity;
            pso.clear_color[3]                  = pso.clear_color[0];
            pso.render_target_depth_texture     = tex_depth;
            pso.clear_depth                     = rhi_depth_load;
            pso.render_target_vrs               = vrs ? GetRenderTarget(Renderer_RenderTarget::shading_rate).get() : nullptr;
            cmd_list->SetPipelineState(pso);

            for (shared_ptr<Entity>& entity : entities)
            {
                // when async loading certain renderable  can be null
                shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>();
                if (!renderable || !renderable->ReadyToRender() || !renderable->IsVisible())
                    continue;

                cmd_list->SetCullMode(static_cast<RHI_CullMode>(renderable->GetMaterial()->GetProperty(MaterialProperty::CullMode)));

                // set vertex, index and instance buffers
                {
                    cmd_list->SetBufferVertex(renderable->GetVertexBuffer());
                    if (pso.instancing)
                    {
                        cmd_list->SetBufferVertex(renderable->GetInstanceBuffer(), 1);
                    }

                    cmd_list->SetBufferIndex(renderable->GetIndexBuffer());
                }

                // set pass constants
                {
                    m_pcb_pass_cpu.transform = entity->GetMatrix();
                    m_pcb_pass_cpu.set_transform_previous(entity->GetMatrixPrevious());
                    m_pcb_pass_cpu.set_is_transparent_and_material_index(is_transparent_pass, renderable->GetMaterial()->GetIndex());
                    PushPassConstants(cmd_list);

                    entity->SetMatrixPrevious(m_pcb_pass_cpu.transform);
                }

                draw_renderable(cmd_list, pso, GetCamera().get(), renderable.get());

                is_first_pass = false;
            }
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Ssgi(RHI_CommandList* cmd_list)
    {
        if (!GetOption<bool>(Renderer_Option::ScreenSpaceGlobalIllumination))
            return;

        // acquire resources
        RHI_Texture* tex_ssgi   = GetRenderTarget(Renderer_RenderTarget::ssgi).get();
        RHI_Shader* shader_ssgi = GetShader(Renderer_Shader::ssgi_c).get();
        if (!shader_ssgi->IsCompiled())
            return;

        cmd_list->BeginTimeblock("ssgi");

        // set pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_ssgi;
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_pcb_pass_cpu.set_resolution_out(tex_ssgi);
        PushPassConstants(cmd_list);

        // set textures
        SetGbufferTextures(cmd_list);
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_ssgi);
        cmd_list->SetTexture(Renderer_BindingsSrv::light_diffuse, GetRenderTarget(Renderer_RenderTarget::light_diffuse));

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_ssgi), thread_group_count_y(tex_ssgi));

        // antiflicker pass to stabilize
        Pass_Antiflicker(cmd_list, tex_ssgi);

        // blur to denoise
        float radius = 8.0f;
        Pass_Blur_Gaussian(cmd_list, tex_ssgi, nullptr, Renderer_Shader::blur_gaussian_bilaterial_c, radius);

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
        RHI_Texture* tex_ssr           = GetRenderTarget(Renderer_RenderTarget::ssr).get();
        RHI_Texture* tex_ssr_roughness = GetRenderTarget(Renderer_RenderTarget::ssr_roughness).get();

        cmd_list->BeginTimeblock(!is_transparent_pass ? "ssr" : "ssr_transparent");

        // set pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_pcb_pass_cpu.set_resolution_out(tex_ssr);
        m_pcb_pass_cpu.set_is_transparent_and_material_index(is_transparent_pass);
        PushPassConstants(cmd_list);

        // set textures
        SetGbufferTextures(cmd_list);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);             // read
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_ssr);            // write
        cmd_list->SetTexture(Renderer_BindingsUav::tex2, tex_ssr_roughness); // write

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_ssr), thread_group_count_y(tex_ssr));

        cmd_list->InsertBarrierTextureReadWrite(tex_ssr_roughness);

        // antiflicker pass to stabilize
        Pass_Antiflicker(cmd_list, tex_ssr);

        // blur based on alpha - which contains the reflection roughness
        Pass_Blur_Gaussian(cmd_list, tex_ssr, tex_ssr_roughness, Renderer_Shader::blur_gaussian_bilaterial_radius_from_texture_c, 0.0f);

        // real time blurring can only go so far, so generate mips that we can use to emulate very high roughness
        Pass_Ffx_Spd(cmd_list, tex_ssr, Renderer_DownsampleFilter::Average);

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Sss(RHI_CommandList* cmd_list)
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
        RHI_Texture* tex_sss = GetRenderTarget(Renderer_RenderTarget::sss).get();

        cmd_list->BeginTimeblock("sss");
        {
            // set pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_c;
            cmd_list->SetPipelineState(pso);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,     GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));  // read from that
            cmd_list->SetTexture(Renderer_BindingsUav::tex_sss, tex_sss); // write to that

            // iterate through all the lights
            static float array_slice_index = 0.0f;
            for (shared_ptr<Entity> entity : entities)
            {
                if (shared_ptr<Light> light = entity->GetComponent<Light>())
                {
                    if (!light->IsFlagSet(LightFlags::ShadowsScreenSpace))
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
                    m_pcb_pass_cpu.set_f3_value(near, far, array_slice_index++);
                    m_pcb_pass_cpu.set_f3_value2(1.0f / tex_sss->GetWidth(), 1.0f / tex_sss->GetHeight(), 0.0f);

                    for (int32_t dispatch_index = 0; dispatch_index < dispatch_list.DispatchCount; ++dispatch_index)
                    {
                        const Bend::DispatchData& dispatch = dispatch_list.Dispatch[dispatch_index];
                        m_pcb_pass_cpu.set_resolution_in({ dispatch.WaveOffset_Shader[0], dispatch.WaveOffset_Shader[1] });
                        PushPassConstants(cmd_list);
                        cmd_list->Dispatch(dispatch.WaveCount[0], dispatch.WaveCount[1], dispatch.WaveCount[2]);
                    }
                }
            }

            array_slice_index = 0;
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Skysphere(RHI_CommandList* cmd_list)
    {
        // acquire shaders
        RHI_Shader* shader_c = GetShader(Renderer_Shader::skysphere_c).get();
        if (!shader_c->IsCompiled())
            return;

        // get directional light
        shared_ptr<Light> light_directional = nullptr;
        {
            const vector<shared_ptr<Entity>>& entities = m_renderables[Renderer_Entity::Light];
            for (size_t i = 0; i < entities.size(); ++i)
            {
                if (shared_ptr<Light> light = entities[i]->GetComponent<Light>())
                {
                    if (light->GetLightType() == LightType::Directional)
                    {
                        light_directional = light;
                        break;
                    }
                }
            }
        }

        if (!light_directional)
            return;

        cmd_list->BeginTimeblock("skysphere");
        {
            // acquire render targets
            RHI_Texture* tex_out = GetRenderTarget(Renderer_RenderTarget::skysphere).get();

            // set pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_c;
            cmd_list->SetPipelineState(pso);

            // set pass constants
            m_pcb_pass_cpu.set_resolution_out(tex_out);
            m_pcb_pass_cpu.set_f3_value2(0.0f, static_cast<float>(light_directional->GetIndex()), 0.0f);
            PushPassConstants(cmd_list);

            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
            cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

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
        RHI_Texture* tex_diffuse    = is_transparent_pass ? GetRenderTarget(Renderer_RenderTarget::light_diffuse_transparent).get()  : GetRenderTarget(Renderer_RenderTarget::light_diffuse).get();
        RHI_Texture* tex_specular   = is_transparent_pass ? GetRenderTarget(Renderer_RenderTarget::light_specular_transparent).get() : GetRenderTarget(Renderer_RenderTarget::light_specular).get();
        RHI_Texture* tex_volumetric = GetRenderTarget(Renderer_RenderTarget::light_volumetric).get();

        // clear render targets
        cmd_list->ClearRenderTarget(tex_diffuse,    Color::standard_black);
        cmd_list->ClearRenderTarget(tex_specular,   Color::standard_black);
        cmd_list->ClearRenderTarget(tex_volumetric, Color::standard_black);

        // define pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;

        // set pipeline state
        cmd_list->SetPipelineState(pso);

        // iterate through all the lights
        static float array_slice_index = 0.0f;
        for (uint32_t light_index = 0; light_index < static_cast<uint32_t>(entities.size()); light_index++)
        {
            if (shared_ptr<Light> light = entities[light_index]->GetComponent<Light>())
            {
                // note: do lighting even at zero intensity as there can be emissive materials

                SetGbufferTextures(cmd_list);
                cmd_list->SetTexture(Renderer_BindingsUav::tex,     tex_diffuse);
                cmd_list->SetTexture(Renderer_BindingsUav::tex2,    tex_specular);
                cmd_list->SetTexture(Renderer_BindingsUav::tex3,    tex_volumetric);
                cmd_list->SetTexture(Renderer_BindingsUav::tex_sss, GetRenderTarget(Renderer_RenderTarget::sss).get());
                cmd_list->SetTexture(Renderer_BindingsSrv::ssgi,    GetRenderTarget(Renderer_RenderTarget::ssgi));
   
                // set shadow maps
                {
                    RHI_Texture* tex_depth = light->IsFlagSet(LightFlags::Shadows)            ? light->GetDepthTexture() : nullptr;
                    RHI_Texture* tex_color = light->IsFlagSet(LightFlags::ShadowsTransparent) ? light->GetColorTexture() : nullptr;

                    if (light->GetLightType() == LightType::Directional)
                    {
                        cmd_list->SetTexture(Renderer_BindingsSrv::light_directional_depth, tex_depth);
                        cmd_list->SetTexture(Renderer_BindingsSrv::light_directional_color, tex_color);
                    }
                    else if (light->GetLightType() == LightType::Point)
                    {
                        cmd_list->SetTexture(Renderer_BindingsSrv::light_point_depth, tex_depth);
                        cmd_list->SetTexture(Renderer_BindingsSrv::light_point_color, tex_color);
                    }
                    else if (light->GetLightType() == LightType::Spot)
                    {
                        cmd_list->SetTexture(Renderer_BindingsSrv::light_spot_depth, tex_depth);
                        cmd_list->SetTexture(Renderer_BindingsSrv::light_spot_color, tex_color);
                    }

                    // light index reads from the texture array index (sss)
                    m_pcb_pass_cpu.set_f3_value2(array_slice_index++, static_cast<float>(light->GetIndex()), 0.0f);
                    cmd_list->SetTexture(Renderer_BindingsSrv::sss, GetRenderTarget(Renderer_RenderTarget::sss));
                }

                // push pass constants
                m_pcb_pass_cpu.set_resolution_out(tex_diffuse);
                m_pcb_pass_cpu.set_is_transparent_and_material_index(is_transparent_pass);
                m_pcb_pass_cpu.set_f3_value(GetOption<float>(Renderer_Option::Fog), GetOption<float>(Renderer_Option::ShadowResolution), 0.0f);
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

        // set pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        cmd_list->SetPipelineState(pso);

        // push pass constants
        m_pcb_pass_cpu.set_resolution_out(tex_out);
        m_pcb_pass_cpu.set_is_transparent_and_material_index(is_transparent_pass);
        m_pcb_pass_cpu.set_f3_value(static_cast<float>(GetRenderTarget(Renderer_RenderTarget::frame_render)->GetMipCount()), GetOption<float>(Renderer_Option::Fog), 0.0f);
        PushPassConstants(cmd_list);

        // set textures
        SetGbufferTextures(cmd_list);
        cmd_list->SetTexture(Renderer_BindingsUav::tex,              tex_out);
        cmd_list->SetTexture(Renderer_BindingsSrv::light_diffuse,    is_transparent_pass ? GetRenderTarget(Renderer_RenderTarget::light_diffuse_transparent).get()  : GetRenderTarget(Renderer_RenderTarget::light_diffuse).get());
        cmd_list->SetTexture(Renderer_BindingsSrv::light_specular,   is_transparent_pass ? GetRenderTarget(Renderer_RenderTarget::light_specular_transparent).get() : GetRenderTarget(Renderer_RenderTarget::light_specular).get());
        cmd_list->SetTexture(Renderer_BindingsSrv::light_volumetric, GetRenderTarget(Renderer_RenderTarget::light_volumetric));
        cmd_list->SetTexture(Renderer_BindingsSrv::frame,            GetRenderTarget(Renderer_RenderTarget::frame_render_2)); // refraction
        cmd_list->SetTexture(Renderer_BindingsSrv::ssgi,             GetRenderTarget(Renderer_RenderTarget::ssgi));
        cmd_list->SetTexture(Renderer_BindingsSrv::environment,      GetRenderTarget(Renderer_RenderTarget::skysphere));

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Light_ImageBased(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass)
    {
        // acquire shader
        RHI_Shader* shader = GetShader(Renderer_Shader::light_image_based_c).get();
        if (!shader->IsCompiled())
            return;

        cmd_list->BeginTimeblock(is_transparent_pass ? "light_image_based_transparent" : "light_image_based");

        // set pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader;
        cmd_list->SetPipelineState(pso);

        // set textures
        SetGbufferTextures(cmd_list);
        cmd_list->SetTexture(Renderer_BindingsSrv::ssgi,        GetRenderTarget(Renderer_RenderTarget::ssgi));
        cmd_list->SetTexture(Renderer_BindingsSrv::ssr,         GetRenderTarget(Renderer_RenderTarget::ssr));
        cmd_list->SetTexture(Renderer_BindingsSrv::sss,         GetRenderTarget(Renderer_RenderTarget::sss));
        cmd_list->SetTexture(Renderer_BindingsSrv::lutIbl,      GetRenderTarget(Renderer_RenderTarget::brdf_specular_lut));
        cmd_list->SetTexture(Renderer_BindingsSrv::environment, GetRenderTarget(Renderer_RenderTarget::skysphere));
        cmd_list->SetTexture(Renderer_BindingsUav::tex,         tex_out);

        // set pass constants
        m_pcb_pass_cpu.set_resolution_out(tex_out);
        m_pcb_pass_cpu.set_is_transparent_and_material_index(is_transparent_pass);
        uint32_t mip_count_skysphere = GetRenderTarget(Renderer_RenderTarget::skysphere)->GetMipCount();
        uint32_t mip_count_ssr       = GetRenderTarget(Renderer_RenderTarget::ssr)->GetMipCount();
        m_pcb_pass_cpu.set_f3_value(static_cast<float>(mip_count_skysphere), static_cast<float>(mip_count_ssr));
        PushPassConstants(cmd_list);

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Blur_Gaussian(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_radius, const Renderer_Shader shader_type, const float radius, const uint32_t mip /*= all_mips*/)
    {
        // acquire shader
        RHI_Shader* shader_c = GetShader(shader_type).get();
        if (!shader_c->IsCompiled())
            return;

        // compute width and height
        const bool mip_requested = mip != rhi_all_mips;
        const uint32_t mip_range = mip_requested ? 1 : 0;
        const uint32_t width     = mip_requested ? (tex_in->GetWidth()  >> mip) : tex_in->GetWidth();
        const uint32_t height    = mip_requested ? (tex_in->GetHeight() >> mip) : tex_in->GetHeight();

        // compute thread group count
        const uint32_t thread_group_count_x_ = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(width) / thread_group_count));
        const uint32_t thread_group_count_y_ = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(height) / thread_group_count));

        // acquire blur scratch buffer
        RHI_Texture* tex_blur = GetRenderTarget(Renderer_RenderTarget::blur).get();
        SP_ASSERT_MSG(width <= tex_blur->GetWidth() && height <= tex_blur->GetHeight(), "Input texture is larger than the blur scratch buffer");

        cmd_list->BeginMarker("blur_gaussian");

        // set pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        cmd_list->SetPipelineState(pso);

        // horizontal pass
        {
            // set pass constants
            m_pcb_pass_cpu.set_resolution_in(Vector2(static_cast<float>(width), static_cast<float>(height)));
            m_pcb_pass_cpu.set_resolution_out(tex_blur);
            m_pcb_pass_cpu.set_f3_value(radius, 0.0f);
            PushPassConstants(cmd_list);

            // set textures
            SetGbufferTextures(cmd_list);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in, mip, mip_range);
            cmd_list->SetTexture(Renderer_BindingsUav::tex2, tex_radius);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_blur); // write

            // render
            cmd_list->Dispatch(thread_group_count_x_, thread_group_count_y_);
        }

        // vertical pass
        {
            // set pass constants
            m_pcb_pass_cpu.set_f3_value(radius, 1.0f);
            PushPassConstants(cmd_list);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_blur);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_in, mip, mip_range); // write

            // render
            cmd_list->Dispatch(thread_group_count_x_, thread_group_count_y_);
        }

        cmd_list->EndMarker();
    }

    void Renderer::Pass_PostProcess(RHI_CommandList* cmd_list)
    {
        // acquire render targets
        RHI_Texture* rt_frame_render         = GetRenderTarget(Renderer_RenderTarget::frame_render).get();
        RHI_Texture* rt_frame_render_scratch = GetRenderTarget(Renderer_RenderTarget::frame_render_2).get();
        RHI_Texture* rt_frame_output         = GetRenderTarget(Renderer_RenderTarget::frame_output).get();
        RHI_Texture* rt_frame_output_scratch = GetRenderTarget(Renderer_RenderTarget::frame_output_2).get();

        cmd_list->BeginMarker("post_proccess");

        // macros which allows us to keep track of which texture is an input/output for each pass
        bool swap_render = true;
        #define get_render_in  swap_render ? rt_frame_render_scratch : rt_frame_render
        #define get_render_out swap_render ? rt_frame_render : rt_frame_render_scratch
        bool swap_output = true;
        #define get_output_in  swap_output ? rt_frame_output_scratch : rt_frame_output
        #define get_output_out swap_output ? rt_frame_output : rt_frame_output_scratch

        // RENDER RESOLUTION
        {
            // Depth of Field
            if (GetOption<bool>(Renderer_Option::DepthOfField))
            {
                swap_render = !swap_render;
                Pass_DepthOfField(cmd_list, get_render_in, get_render_out);
            }
        }

        // deduce some information
        Renderer_Upsampling upsampling_mode = GetOption<Renderer_Upsampling>(Renderer_Option::Upsampling);
        Renderer_Antialiasing antialiasing  = GetOption<Renderer_Antialiasing>(Renderer_Option::Antialiasing);
        bool taa_enabled                    = antialiasing == Renderer_Antialiasing::Taa  || antialiasing == Renderer_Antialiasing::TaaFxaa;
        bool fxaa_enabled                   = antialiasing == Renderer_Antialiasing::Fxaa || antialiasing == Renderer_Antialiasing::TaaFxaa;

        // RENDER RESOLUTION -> OUTPUT RESOLUTION
        {
            // if render resolution equals output resolution, FSR 2 will act as TAA

            swap_render = !swap_render;

            // use FSR 2 for different resolutions if enabled, otherwise blit
            if (upsampling_mode == Renderer_Upsampling::FSR2)
            {
                Pass_Ffx_Fsr2(cmd_list, get_render_in, rt_frame_output);
            }
            else
            {
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

    void Renderer::Pass_Taa(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // acquire shader
        RHI_Shader* shader_c = GetShader(Renderer_Shader::taa_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginMarker("taa");

        RHI_Texture* tex_history = GetRenderTarget(Renderer_RenderTarget::frame_render_history).get();

        // set pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_pcb_pass_cpu.set_resolution_out(tex_out);
        PushPassConstants(cmd_list);

        // set textures
        SetGbufferTextures(cmd_list);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_in);      // input
        cmd_list->SetTexture(Renderer_BindingsSrv::tex,  tex_history); // history
        cmd_list->SetTexture(Renderer_BindingsUav::tex,  tex_out);     // output

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        // record history
        cmd_list->Blit(tex_out, tex_history, false);

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
        RHI_Texture* tex_bloom = GetRenderTarget(Renderer_RenderTarget::bloom).get();

        // Luminance
        cmd_list->BeginMarker("luminance");
        {
            // Define pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_luminance;

            // Set pipeline state
            cmd_list->SetPipelineState(pso);

            // Set pass constants
            m_pcb_pass_cpu.set_resolution_out(tex_bloom);
            PushPassConstants(cmd_list);

            // Set textures
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_bloom);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

            // Render
            cmd_list->Dispatch(thread_group_count_x(tex_bloom), thread_group_count_y(tex_bloom));
        }
        cmd_list->EndMarker();

        // Generate mips
        Pass_Ffx_Spd(cmd_list, tex_bloom, Renderer_DownsampleFilter::Antiflicker);

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
                m_pcb_pass_cpu.set_resolution_out(Vector2(static_cast<float>(mip_width_large), static_cast<float>(mip_height_height)));
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
            m_pcb_pass_cpu.set_resolution_out(tex_out);
            m_pcb_pass_cpu.set_f3_value(GetOption<float>(Renderer_Option::Bloom), 0.0f, 0.0f);
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
        m_pcb_pass_cpu.set_resolution_out(tex_out);
        m_pcb_pass_cpu.set_f3_value(Display::GetLuminanceMax(), GetOption<float>(Renderer_Option::Tonemapping), GetOption<float>(Renderer_Option::Exposure));
        m_pcb_pass_cpu.set_f3_value2(GetOption<float>(Renderer_Option::Hdr), 0.0f, 0.0f);
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
        // acquire shader
        RHI_Shader* shader_c = GetShader(Renderer_Shader::fxaa_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock("fxaa");

        // set pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_pcb_pass_cpu.set_resolution_out(tex_out);
        PushPassConstants(cmd_list);

        // set textures
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        
        // render
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
        m_pcb_pass_cpu.set_resolution_out(tex_out);
        m_pcb_pass_cpu.set_f3_value(m_camera->GetAperture(), 0.0f, 0.0f);
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
        m_pcb_pass_cpu.set_resolution_out(tex_out);
        m_pcb_pass_cpu.set_f3_value(m_camera->GetShutterSpeed(), 0.0f, 0.0f);
        PushPassConstants(cmd_list);

        // set textures
        SetGbufferTextures(cmd_list);
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_DepthOfField(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // acquire shader
        RHI_Shader* shader_c = GetShader(Renderer_Shader::depth_of_field_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock("depth_of_field");

        // set pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_pcb_pass_cpu.set_resolution_out(tex_out);
        m_pcb_pass_cpu.set_f3_value(m_camera->GetAperture(), 0.0f, 0.0f);
        PushPassConstants(cmd_list);

        // set textures
        SetGbufferTextures(cmd_list);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

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
        m_pcb_pass_cpu.set_resolution_out(tex_out);
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
        // acquire shader
        RHI_Shader* shader_c = GetShader(Renderer_Shader::film_grain_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock("film_grain");

        // set pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_pcb_pass_cpu.set_resolution_out(tex_out);
        m_pcb_pass_cpu.set_f3_value(m_camera->GetIso(), 0.0f, 0.0f);
        PushPassConstants(cmd_list);

        // set textures
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Antiflicker(RHI_CommandList* cmd_list, RHI_Texture* tex_in)
    {
        // acquire shader
        RHI_Shader* shader_c = GetShader(Renderer_Shader::antiflicker_c).get();
        if (!shader_c->IsCompiled())
            return;

        cmd_list->BeginMarker("antiflicker");

        RHI_Texture* tex_scratch = GetRenderTarget(Renderer_RenderTarget::antiflicker).get();

        // set pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_pcb_pass_cpu.set_resolution_out(tex_in);
        PushPassConstants(cmd_list);

        // render
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_scratch);
        cmd_list->Dispatch(thread_group_count_x(tex_in), thread_group_count_y(tex_in));

        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_scratch);
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_in);
        cmd_list->Dispatch(thread_group_count_x(tex_in), thread_group_count_y(tex_in));

        cmd_list->EndMarker();
    }

    void Renderer::Pass_Ffx_Cas(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // acquire shaders
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
            m_pcb_pass_cpu.set_resolution_out(tex_out);
            m_pcb_pass_cpu.set_f3_value(sharpness, 0.0f, 0.0f);
            PushPassConstants(cmd_list);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);

            // render
            cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));

            cmd_list->EndTimeblock();
        }
    }

    void Renderer::Pass_Ffx_Spd(RHI_CommandList* cmd_list, RHI_Texture* tex, const Renderer_DownsampleFilter filter, const uint32_t mip_start)
    {
        // AMD FidelityFX Single Pass Downsampler.
        // Provides an RDNA™-optimized solution for generating up to 12 MIP levels of a texture.
        // GitHub:        https://github.com/GPUOpen-Effects/FidelityFX-SPD
        // Documentation: https://github.com/GPUOpen-Effects/FidelityFX-SPD/blob/master/docs/FidelityFX_SPD.pdf

        // deduce information
        const uint32_t output_mip_count      = tex->GetMipCount() - (mip_start + 1);
        const uint32_t width                 = tex->GetWidth()  >> mip_start;
        const uint32_t height                = tex->GetHeight() >> mip_start;
        const uint32_t thread_group_count_x_ = (width + 63)  >> 6; // as per document documentation (page 22)
        const uint32_t thread_group_count_y_ = (height + 63) >> 6; // as per document documentation (page 22)

        // ensure that the input texture meets the requirements
        SP_ASSERT(tex->HasPerMipViews());
        SP_ASSERT(width <= 4096 && height <= 4096 && output_mip_count <= 12); // as per documentation (page 22)
        SP_ASSERT(mip_start < output_mip_count);

        // acquire shader
        RHI_Shader* shader_c = nullptr;
        {
            // deduce appropriate shader
            Renderer_Shader shader = Renderer_Shader::ffx_spd_average_c;
            if (filter == Renderer_DownsampleFilter::Highest)     shader = Renderer_Shader::ffx_spd_highest_c;
            if (filter == Renderer_DownsampleFilter::Antiflicker) shader = Renderer_Shader::ffx_spd_antiflicker_c;

            shader_c = GetShader(shader).get();
            if (!shader_c->IsCompiled())
                return;
        }

        cmd_list->BeginMarker("ffx_spd");
        {
            // set pipeline state
            static RHI_PipelineState pso;
            pso.name           = "ffx_spd";
            pso.shader_compute = shader_c;
            cmd_list->SetPipelineState(pso);

            // push pass data
            m_pcb_pass_cpu.set_resolution_out(tex);
            m_pcb_pass_cpu.set_f3_value(static_cast<float>(output_mip_count), static_cast<float>(thread_group_count_x_ * thread_group_count_y_), 0.0f);
            PushPassConstants(cmd_list);

            // set textures
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,     tex, mip_start, 1);                    // starting mip
            cmd_list->SetTexture(Renderer_BindingsUav::tex_spd, tex, mip_start + 1, output_mip_count); // following mips

            // render
            cmd_list->Dispatch(thread_group_count_x_, thread_group_count_y_);
        }
        cmd_list->EndMarker();
    }

    void Renderer::Pass_Ffx_Fsr2(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        cmd_list->BeginTimeblock("amd_ffx_fsr2");

        bool is_upsampling = GetResolutionRender().x < GetResolutionOutput().x || GetResolutionRender().y < GetResolutionOutput().y;
        float sharpness    = is_upsampling ? GetOption<float>(Renderer_Option::Sharpness) : 0.0f; // if not upsampling we do Pass_Ffx_Cas()

        RHI_FidelityFX::FSR2_Dispatch(
            cmd_list,
            tex_in,
            GetRenderTarget(Renderer_RenderTarget::frame_render_opaque).get(),
            GetRenderTarget(Renderer_RenderTarget::gbuffer_depth).get(),
            GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity).get(),
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

        // set pipeline state
        cmd_list->SetPipelineState(pso);

        auto draw_icon = [&cmd_list](Entity* entity, RHI_Texture* texture)
        {
            const Vector3 pos_world        = entity->GetPosition();
            const Vector3 pos_world_camera = GetCamera()->GetEntity()->GetPosition();
            const Vector3 camera_to_light  = (pos_world - pos_world_camera).Normalized();
            const float v_dot_l            = Vector3::Dot(GetCamera()->GetEntity()->GetForward(), camera_to_light);

            // only draw if it's inside our view
            if (v_dot_l > 0.5f)
            {
                // compute transform
                {
                    // use the distance from the camera to scale the icon, this will
                    // cancel out perspective scaling, hence keeping the icon scale constant
                    const float distance = (pos_world_camera - pos_world).Length();
                    const float scale = distance * 0.04f;

                    // 1st rotation: The quad's normal is parallel to the world's Y axis, so we rotate to make it camera facing
                    Quaternion rotation_reorient_quad = Quaternion::FromEulerAngles(-90.0f, 0.0f, 0.0f);
                    // 2nd rotation: Rotate the camera facing quad with the camera, so that it remains a camera facing quad
                    Quaternion rotation_camera_billboard = Quaternion::FromLookRotation(pos_world - pos_world_camera);

                    Matrix transform = Matrix(pos_world, rotation_camera_billboard * rotation_reorient_quad, scale);

                    // set transform
                    m_pcb_pass_cpu.transform = transform * m_cb_frame_cpu.view_projection_unjittered;
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
            draw_icon(entity.get(), GetStandardTexture(Renderer_StandardTexture::Gizmo_audio_source).get());
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

            draw_icon(entity.get(), texture);
        }

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Grid(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (!GetOption<bool>(Renderer_Option::Debug_Grid))
            return;

        // acquire shader
        RHI_Shader* shader_v = GetShader(Renderer_Shader::grid_v).get();
        RHI_Shader* shader_p = GetShader(Renderer_Shader::grid_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        RHI_Texture* tex_depth = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_output).get();

        cmd_list->BeginTimeblock("grid");

        // set pipeline state
        static RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v;
        pso.shader_pixel                    = shader_p;
        pso.rasterizer_state                = GetRasterizerState(Renderer_RasterizerState::Solid_cull_none).get();
        pso.blend_state                     = GetBlendState(Renderer_BlendState::Alpha).get();
        pso.depth_stencil_state             = GetDepthStencilState(Renderer_DepthStencilState::Depth_read).get();
        pso.render_target_color_textures[0] = tex_out;
        pso.render_target_depth_texture     = tex_depth;
        cmd_list->SetPipelineState(pso);

        // set transform
        {
            // follow camera in world unit increments so that the grid appears stationary in relation to the camera
            const float grid_spacing       = 1.0f;
            const Vector3& camera_position = m_camera->GetEntity()->GetPosition();
            const Vector3 translation      = Vector3(
                floor(camera_position.x / grid_spacing) * grid_spacing,
                0.0f,
                floor(camera_position.z / grid_spacing) * grid_spacing
            );

            m_pcb_pass_cpu.transform = Matrix::CreateScale(Vector3(1000.0f, 1.0f, 1000.0f)) * Matrix::CreateTranslation(translation);

            PushPassConstants(cmd_list);
        }

        cmd_list->SetBufferVertex(GetStandardMesh(Renderer_MeshType::Quad)->GetVertexBuffer());
        cmd_list->SetBufferIndex(GetStandardMesh(Renderer_MeshType::Quad)->GetIndexBuffer());
        cmd_list->DrawIndexed(6);

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Lines(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        // acquire shaders
        RHI_Shader* shader_v = GetShader(Renderer_Shader::line_v).get();
        RHI_Shader* shader_p = GetShader(Renderer_Shader::line_p).get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        AddLinesToBeRendered();

        cmd_list->BeginTimeblock("lines");

        // set pipeline state
        static RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v;
        pso.shader_pixel                    = shader_p;
        pso.rasterizer_state                = GetRasterizerState(Renderer_RasterizerState::Wireframe_cull_none).get();
        pso.render_target_color_textures[0] = tex_out;
        pso.clear_color[0]                  = rhi_color_load;
        pso.render_target_depth_texture     = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_output).get();

        // world space rendering
        m_pcb_pass_cpu.transform = Matrix::Identity;
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
                    pso.primitive_toplogy   = RHI_PrimitiveTopology::LineList;
                    cmd_list->SetPipelineState(pso);

                    cmd_list->SetBufferVertex(m_vertex_buffer_lines.get());
                    cmd_list->Draw(m_lines_index_depth_off + 1);

                    cmd_list->EndMarker();
                }

                // depth on
                if (m_lines_index_depth_on > (vertex_count / 2) - 1)
                {
                    cmd_list->BeginMarker("depth_on");

                    // set pipeline state
                    pso.blend_state         = GetBlendState(Renderer_BlendState::Alpha).get();
                    pso.depth_stencil_state = GetDepthStencilState(Renderer_DepthStencilState::Depth_read).get();
                    pso.primitive_toplogy   = RHI_PrimitiveTopology::LineList;
                    cmd_list->SetPipelineState(pso);

                    cmd_list->SetBufferVertex(m_vertex_buffer_lines.get());
                    cmd_list->Draw((m_lines_index_depth_on - (vertex_count / 2)) + 1, vertex_count / 2);

                    cmd_list->EndMarker();
                }
            }
        }

        m_lines_index_depth_off = numeric_limits<uint32_t>::max();                         // max +1 will wrap it to 0
        m_lines_index_depth_on  = (static_cast<uint32_t>(m_line_vertices.size()) / 2) - 1; // -1 because +1 will make it go to size / 2

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
                    RHI_Texture* tex_outline = GetRenderTarget(Renderer_RenderTarget::outline).get();

                    if (shared_ptr<Renderable> renderable = entity_selected->GetComponent<Renderable>())
                    {
                        cmd_list->BeginMarker("color_silhouette");
                        {
                            // set pipeline state
                            static RHI_PipelineState pso;
                            pso.shader_vertex                   = shader_v;
                            pso.shader_pixel                    = shader_p;
                            pso.rasterizer_state                = GetRasterizerState(Renderer_RasterizerState::Solid_cull_back).get();
                            pso.blend_state                     = GetBlendState(Renderer_BlendState::Disabled).get();
                            pso.depth_stencil_state             = GetDepthStencilState(Renderer_DepthStencilState::Off).get();
                            pso.render_target_color_textures[0] = tex_outline;
                            pso.clear_color[0]                  = Color::standard_transparent;
                            cmd_list->SetPipelineState(pso);
                        
                            // render
                            {
                                // push draw data
                                m_pcb_pass_cpu.set_f4_value(Color::standard_renderer_lines);
                                m_pcb_pass_cpu.transform = entity_selected->GetMatrix();
                                PushPassConstants(cmd_list);
                        
                                cmd_list->SetBufferVertex(renderable->GetVertexBuffer());
                                cmd_list->SetBufferIndex(renderable->GetIndexBuffer());
                                cmd_list->DrawIndexed(renderable->GetIndexCount(), renderable->GetIndexOffset(), renderable->GetVertexOffset());
                            }
                        }
                        cmd_list->EndMarker();
                        
                        // blur the color silhouette
                        {
                            const float radius = 30.0f;
                            Pass_Blur_Gaussian(cmd_list, tex_outline, nullptr, Renderer_Shader::blur_gaussian_c, radius);
                        }
                        
                        // combine color silhouette with frame
                        cmd_list->BeginMarker("composition");
                        {
                            // set pipeline state
                            static RHI_PipelineState pso;
                            pso.shader_compute = shader_c;
                            cmd_list->SetPipelineState(pso);
                        
                            // set pass constants
                            m_pcb_pass_cpu.set_resolution_out(tex_out);
                            PushPassConstants(cmd_list);
                        
                            // set textures
                            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
                            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_outline);
                        
                            // render
                            cmd_list->Dispatch(thread_group_count_x(tex_out), thread_group_count_y(tex_out));
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
        // early exit cases
        const bool draw       = GetOption<bool>(Renderer_Option::Debug_PerformanceMetrics);
        const auto& shader_v  = GetShader(Renderer_Shader::font_v);
        const auto& shader_p  = GetShader(Renderer_Shader::font_p);
        shared_ptr<Font> font = GetFont();
        if (!shader_v || !shader_v->IsCompiled() || !shader_p || !shader_p->IsCompiled() || !draw || !font->HasText())
            return;

        cmd_list->BeginMarker("text");

        // set pipeline state
        static RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v.get();
        pso.shader_pixel                    = shader_p.get();
        pso.rasterizer_state                = GetRasterizerState(Renderer_RasterizerState::Solid_cull_back).get();
        pso.blend_state                     = GetBlendState(Renderer_BlendState::Alpha).get();
        pso.depth_stencil_state             = GetDepthStencilState(Renderer_DepthStencilState::Off).get();
        pso.render_target_color_textures[0] = tex_out;
        pso.clear_color[0]                  = rhi_color_load;
        pso.name                            = "Pass_Text";
        cmd_list->SetPipelineState(pso);

        // set vertex and index buffer
        font->UpdateVertexAndIndexBuffers();
        cmd_list->SetBufferVertex(font->GetVertexBuffer());
        cmd_list->SetBufferIndex(font->GetIndexBuffer());

        // outline
        cmd_list->BeginTimeblock("text_outline");
        if (font->GetOutline() != Font_Outline_None && font->GetOutlineSize() != 0)
        {
            // set pass constants
            m_pcb_pass_cpu.set_f4_value(font->GetColorOutline());
            PushPassConstants(cmd_list);

            // draw
            cmd_list->SetTexture(Renderer_BindingsSrv::font_atlas, font->GetAtlasOutline());
            cmd_list->DrawIndexed(font->GetIndexCount());
        }
        cmd_list->EndTimeblock();

        // inline
        cmd_list->BeginTimeblock("text_inline");
        {
            // set pass constants
            m_pcb_pass_cpu.set_f4_value(font->GetColor());
            PushPassConstants(cmd_list);

            // draw
            cmd_list->SetTexture(Renderer_BindingsSrv::font_atlas, font->GetAtlas());
            cmd_list->DrawIndexed(font->GetIndexCount());
        }
        cmd_list->EndTimeblock();

        cmd_list->EndMarker();
    }

    void Renderer::Pass_Light_Integration_BrdfSpecularLut(RHI_CommandList* cmd_list)
    {
        // acquire shader
        RHI_Shader* shader_c = GetShader(Renderer_Shader::light_integration_brdf_specular_lut_c).get();
        if (!shader_c || !shader_c->IsCompiled())
            return;

        cmd_list->BeginTimeblock("light_integration_brdf_specular_lut");

        // acquire render target
        RHI_Texture* tex_brdf_specular_lut = GetRenderTarget(Renderer_RenderTarget::brdf_specular_lut).get();

        // set pipeline state
        static RHI_PipelineState pso;
        pso.shader_compute = shader_c;
        cmd_list->SetPipelineState(pso);

        // set pass constants
        m_pcb_pass_cpu.set_resolution_out(tex_brdf_specular_lut);
        PushPassConstants(cmd_list);

        // set texture
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_brdf_specular_lut);

        // render
        cmd_list->Dispatch(thread_group_count_x(tex_brdf_specular_lut), thread_group_count_y(tex_brdf_specular_lut));

        cmd_list->EndTimeblock();
        light_integration_brdf_speculat_lut_completed = true;
    }

    void Renderer::Pass_Light_Integration_EnvironmentPrefilter(RHI_CommandList* cmd_list)
    {
        // acquire shader
        RHI_Shader* shader_c = GetShader(Renderer_Shader::light_integration_environment_filter_c).get();
        if (!shader_c || !shader_c->IsCompiled())
            return;

        // acquire render target
        RHI_Texture* tex_environment = GetRenderTarget(Renderer_RenderTarget::skysphere).get();

        cmd_list->BeginTimeblock("light_integration_environment_filter");
        {
            // set pipeline state
            static RHI_PipelineState pso;
            pso.shader_compute = shader_c;
            cmd_list->SetPipelineState(pso);

            uint32_t mip_count = tex_environment->GetMipCount();
            uint32_t mip_level = mip_count - m_environment_mips_to_filter_count;
            SP_ASSERT(mip_level != 0);

            // read from the previous mip (not the top) - this helps accumulate filtering without doing a lot of samples
            cmd_list->SetTexture(Renderer_BindingsSrv::environment, tex_environment, mip_level - 1, 1);

            // do one mip at a time, splitting the cost over a couple of frames
            Vector2 resolution = Vector2(tex_environment->GetWidth() >> mip_level, tex_environment->GetHeight() >> mip_level);
            {
                // set pass constants
                m_pcb_pass_cpu.set_resolution_out(resolution);
                m_pcb_pass_cpu.set_f3_value(static_cast<float>(mip_level), static_cast<float>(mip_count), 0.0f);
                PushPassConstants(cmd_list);

                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_environment, mip_level, 1);
                cmd_list->Dispatch(
                    static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(resolution.x) / thread_group_count)),
                    static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(resolution.y) / thread_group_count))
                );
            }

            m_environment_mips_to_filter_count--;

            // the first two filtered mips have obvious sample patterns, so blur them
            if (m_environment_mips_to_filter_count == 0)
            {
                Pass_Blur_Gaussian(cmd_list, tex_environment, nullptr, Renderer_Shader::blur_gaussian_c, 32.0f, 1);
                Pass_Blur_Gaussian(cmd_list, tex_environment, nullptr, Renderer_Shader::blur_gaussian_c, 32.0f, 2);
            }
        }
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
        Pass_Ffx_Spd(cmd_list, texture, Renderer_DownsampleFilter::Average);

        // set all generated mips to read only optimal
        texture->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list, 0, texture->GetMipCount());

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
