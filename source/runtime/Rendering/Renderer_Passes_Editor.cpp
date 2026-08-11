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

//= INCLUDES ==================================
#include "pch.h"
#include "Renderer.h"
#include "../Geometry/Mesh.h"
#include "../World/Entity.h"
#include "../World/World.h"
#include "../World/Components/Component.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Components/AudioSource.h"
#include "../World/Components/ParticleSystem.h"
#include "../World/Components/Render.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Buffer.h"
#include "../RHI/RHI_Shader.h"
#include "../Rendering/Material.h"
//=============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        // pick one gizmo for an entity, lights win, render-only meshes are skipped
        RHI_Texture* entity_gizmo_texture(Entity* entity)
        {
            const uint32_t component_count = entity->GetComponentCount();
            if (component_count == 0)
            {
                return nullptr;
            }

            // most scene props are render-only, bail before the component probe list
            if (component_count == 1 && entity->GetComponent<Render>())
            {
                return nullptr;
            }

            if (Light* light = entity->GetComponent<Light>())
            {
                if (light->GetLightType() == LightType::Directional)
                {
                    return Renderer::GetStandardTexture(Renderer_StandardTexture::Gizmo_light_directional);
                }
                if (light->GetLightType() == LightType::Point)
                {
                    return Renderer::GetStandardTexture(Renderer_StandardTexture::Gizmo_light_point);
                }
                if (light->GetLightType() == LightType::Spot)
                {
                    return Renderer::GetStandardTexture(Renderer_StandardTexture::Gizmo_light_spot);
                }
            }

            static const pair<ComponentType, Renderer_StandardTexture> priority[] =
            {
                { ComponentType::Camera,         Renderer_StandardTexture::Gizmo_camera          },
                { ComponentType::AudioSource,    Renderer_StandardTexture::Gizmo_audio_source    },
                { ComponentType::ParticleSystem, Renderer_StandardTexture::Gizmo_particle        },
                { ComponentType::Volume,         Renderer_StandardTexture::Gizmo_volume          },
                { ComponentType::SpawnPoint,     Renderer_StandardTexture::Gizmo_spawn_point     },
                { ComponentType::Terrain,        Renderer_StandardTexture::Gizmo_terrain         },
                { ComponentType::Water,          Renderer_StandardTexture::Gizmo_water           },
                { ComponentType::Physics,        Renderer_StandardTexture::Gizmo_physics         },
                { ComponentType::Spline,         Renderer_StandardTexture::Gizmo_spline          },
                { ComponentType::SplineFollower, Renderer_StandardTexture::Gizmo_spline_follower },
                { ComponentType::Traffic,        Renderer_StandardTexture::Gizmo_traffic         },
                { ComponentType::Pedestrians,    Renderer_StandardTexture::Gizmo_pedestrians     },
                { ComponentType::Animator,       Renderer_StandardTexture::Gizmo_animator        },
                { ComponentType::Ragdoll,        Renderer_StandardTexture::Gizmo_ragdoll         },
                { ComponentType::SkidMarks,      Renderer_StandardTexture::Gizmo_skid_marks      },
                { ComponentType::CarReset,       Renderer_StandardTexture::Gizmo_car_reset       },
                { ComponentType::Text3D,         Renderer_StandardTexture::Gizmo_text_3d         },
                { ComponentType::Script,         Renderer_StandardTexture::Gizmo_script          },
            };

            for (const auto& entry : priority)
            {
                if (entity->GetComponentByType(entry.first))
                {
                    return Renderer::GetStandardTexture(entry.second);
                }
            }

            return nullptr;
        }
    }

    void Renderer::Pass_Icons(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        static uint64_t icons_frame = ~0ull;
        if (icons_frame != m_frame_num)
        {
            icons_frame = m_frame_num;

            if (!Engine::IsFlagSet(EngineMode::Playing) && cvar_entity_icons.GetValueAs<bool>())
            {
                const Vector3 pos_camera = World::GetCamera() ? World::GetCamera()->GetEntity()->GetPosition() : Vector3::Zero;
                const Vector3 cam_forward = World::GetCamera() ? World::GetCamera()->GetEntity()->GetForward() : Vector3::Forward;

                for (Entity* entity : World::GetEntitiesWithIcon())
                {
                    const Vector3 pos = entity->GetPosition();
                    const Vector3 to_icon = pos - pos_camera;
                    const float dist_sq = to_icon.LengthSquared();

                    // skip icons too close; keep them visible across large open worlds
                    constexpr float icon_max_distance = 4000.0f;
                    if (dist_sq <= 0.01f || dist_sq > (icon_max_distance * icon_max_distance))
                    {
                        continue;
                    }

                    // match shader front-face cull so we never enqueue invisible icons
                    const float facing = Vector3::Dot(cam_forward, to_icon);
                    if (facing <= 0.0f || (facing * facing) <= (0.25f * dist_sq))
                    {
                        continue;
                    }

                    if (RHI_Texture* texture = entity_gizmo_texture(entity))
                    {
                        m_icons.emplace_back(make_tuple(texture, pos));
                    }
                }
            }
        }

        if (m_icons.empty())
        {
            return;
        }

        cmd_list->BeginTimeblock("icons");
        {
            // group by texture so each atlas/gizmo type is one draw
            sort(m_icons.begin(), m_icons.end(), [](const tuple<RHI_Texture*, Vector3>& a, const tuple<RHI_Texture*, Vector3>& b)
            {
                return get<0>(a) < get<0>(b);
            });

            static const Vector2 corners[6] =
            {
                Vector2(-1.0f, -1.0f),
                Vector2(-1.0f,  1.0f),
                Vector2( 1.0f, -1.0f),
                Vector2( 1.0f, -1.0f),
                Vector2(-1.0f,  1.0f),
                Vector2( 1.0f,  1.0f)
            };

            m_icons_vertices.clear();
            m_icons_vertices.reserve(m_icons.size() * 6);
            for (const auto& [texture, pos_world] : m_icons)
            {
                if (!texture)
                {
                    continue;
                }

                for (const Vector2& corner : corners)
                {
                    m_icons_vertices.emplace_back(pos_world, corner);
                }
            }

            if (m_icons_vertices.empty())
            {
                cmd_list->EndTimeblock();
                return;
            }

            const uint32_t vertex_count = static_cast<uint32_t>(m_icons_vertices.size());
            if (!m_icons_vertex_buffer || vertex_count > m_icons_vertex_buffer->GetElementCount())
            {
                m_icons_vertex_buffer = make_shared<RHI_Buffer>(
                    RHI_Buffer_Type::Vertex,
                    sizeof(m_icons_vertices[0]),
                    vertex_count,
                    static_cast<void*>(m_icons_vertices.data()),
                    true,
                    "icons"
                );
            }
            else
            {
                RHI_Vertex_PosTex* buffer = static_cast<RHI_Vertex_PosTex*>(m_icons_vertex_buffer->GetMappedData());
                copy(m_icons_vertices.begin(), m_icons_vertices.end(), buffer);
            }

            RHI_PipelineState pso;
            pso.name                             = "icons";
            pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::icon_v);
            pso.shaders[RHI_Shader_Type::Pixel]  = GetShader(Renderer_Shader::icon_p);
            pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Solid);
            pso.blend_state                      = GetBlendState(Renderer_BlendState::Alpha);
            pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::Off);
            pso.render_target_color_textures[0]  = tex_out;
            pso.clear_color[0]                   = rhi_color_load;
            cmd_list->SetPipelineState(pso);

            m_pcb_pass_cpu.set_f2_value(static_cast<float>(renderer_editor_icon_size_px), static_cast<float>(renderer_editor_icon_size_px));
            m_pcb_pass_cpu.set_f4_value(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()), 0.0f, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->SetBufferVertex(m_icons_vertex_buffer.get());
            cmd_list->SetCullMode(RHI_CullMode::None);

            uint32_t vertex_offset = 0;
            RHI_Texture* current_texture = nullptr;
            uint32_t run_icons = 0;

            auto flush_run = [&]()
            {
                if (run_icons == 0 || !current_texture)
                {
                    return;
                }

                cmd_list->SetTexture(Renderer_BindingsSrv::tex, current_texture);
                cmd_list->Draw(run_icons * 6, vertex_offset);
                vertex_offset += run_icons * 6;
                run_icons = 0;
            };

            for (const auto& [texture, pos_world] : m_icons)
            {
                if (!texture)
                {
                    continue;
                }

                if (texture != current_texture)
                {
                    flush_run();
                    current_texture = texture;
                }

                run_icons++;
            }
            flush_run();

            cmd_list->SetCullMode(RHI_CullMode::Back);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Grid(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (!cvar_grid.GetValueAs<bool>())
        {
            return;
        }

        RHI_Shader* shader_v = GetShader(Renderer_Shader::grid_v);
        RHI_Shader* shader_p = GetShader(Renderer_Shader::grid_p);

        cmd_list->BeginTimeblock("grid");

        RHI_PipelineState pso;
        pso.name                             = "grid";
        pso.shaders[RHI_Shader_Type::Vertex] = shader_v;
        pso.shaders[RHI_Shader_Type::Pixel]  = shader_p;
        pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Solid);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Alpha);
        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::Off);
        pso.render_target_color_textures[0]  = tex_out;
        cmd_list->SetPipelineState(pso);
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_depth, GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output));

        // follow camera in world-unit increments so the grid appears stationary
        {
            const float grid_spacing       = 1.0f;
            const Vector3& camera_position = World::GetCamera()->GetEntity()->GetPosition();
            const Vector3 translation      = Vector3(
                floor(camera_position.x / grid_spacing) * grid_spacing,
                0.0f,
                floor(camera_position.z / grid_spacing) * grid_spacing
            );

            Matrix grid_transform     = Matrix::CreateScale(Vector3(1000.0f, 1.0f, 1000.0f)) * Matrix::CreateTranslation(translation);
            m_pcb_pass_cpu.draw_index = WriteDrawData(grid_transform);
            if (m_pcb_pass_cpu.draw_index == numeric_limits<uint32_t>::max())
            {
                cmd_list->EndTimeblock();
                return;
            }
            cmd_list->PushConstants(m_pcb_pass_cpu);
        }

        cmd_list->SetCullMode(RHI_CullMode::Back);
        cmd_list->SetBufferVertex(GetStandardMesh(MeshType::Quad)->GetVertexBuffer());
        cmd_list->SetBufferIndex(GetStandardMesh(MeshType::Quad)->GetIndexBuffer());
        cmd_list->DrawIndexed(6, GetStandardMesh(MeshType::Quad)->GetGlobalIndexOffset(), GetStandardMesh(MeshType::Quad)->GetGlobalVertexOffset());

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

            RHI_PipelineState pso;
            pso.name                             = "lines";
            pso.shaders[RHI_Shader_Type::Vertex] = shader_v;
            pso.shaders[RHI_Shader_Type::Pixel]  = shader_p;
            pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Wireframe);
            pso.blend_state                      = GetBlendState(Renderer_BlendState::Alpha);
            pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::Off);
            pso.render_target_color_textures[0]  = tex_out;
            pso.clear_color[0]                   = rhi_color_load;
            pso.primitive_topology               = RHI_PrimitiveTopology::LineList;
            cmd_list->SetPipelineState(pso);
            cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_depth, GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output));

            if (vertex_count > m_lines_vertex_buffer->GetElementCount())
            {
                m_lines_vertex_buffer = make_shared<RHI_Buffer>(RHI_Buffer_Type::Vertex, sizeof(m_lines_vertices[0]), vertex_count, static_cast<void*>(&m_lines_vertices[0]), true, "lines");
            }

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
        {
            return;
        }

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

                    bool any_rendered = false;
                    cmd_list->BeginMarker("color_silhouette");
                    {
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
                    
                        for (Entity* entity_selected : selected_entities)
                        {
                            if (!entity_selected)
                            {
                                continue;
                            }
                                
                            Render* render = entity_selected->GetComponent<Render>();
                            if (!render)
                            {
                                continue;
                            }
                                
                            if (!render->GetVertexBuffer() || !render->GetIndexBuffer())
                            {
                                continue;
                            }

                            const uint32_t draw_index = WriteDrawData(entity_selected->GetMatrix());
                            if (draw_index == numeric_limits<uint32_t>::max())
                            {
                                continue;
                            }
                            m_pcb_pass_cpu.draw_index = draw_index;
                            m_pcb_pass_cpu.set_f4_value(Color::standard_renderer_lines);
                            cmd_list->PushConstants(m_pcb_pass_cpu);

                            cmd_list->SetBufferVertex(render->GetVertexBuffer());
                            cmd_list->SetBufferIndex(render->GetIndexBuffer());
                            cmd_list->DrawIndexed(render->GetIndexCount(), render->GetIndexOffset(), render->GetVertexOffset());
                            any_rendered = true;
                        }
                    }
                    cmd_list->EndMarker();
                    
                    if (any_rendered)
                    {
                        {
                            const float radius = 30.0f;
                            Pass_Blur(cmd_list, tex_outline, false, radius);
                        }
                        
                        cmd_list->BeginMarker("composition");
                        {
                            RHI_PipelineState pso;
                            pso.name             = "composition";
                            pso.shaders[Compute] = shader_c;
                            cmd_list->SetPipelineState(pso);

                            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
                            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_outline);
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
        const auto& shader_v  = GetShader(Renderer_Shader::font_v);
        const auto& shader_p  = GetShader(Renderer_Shader::font_p);
        shared_ptr<Font> font = GetFont();

        if (!font->HasText())
        {
            return;
        }

        cmd_list->BeginTimeblock("text");

        font->UpdateVertexAndIndexBuffers(cmd_list);

        RHI_PipelineState pso;
        pso.name                             = "text";
        pso.shaders[RHI_Shader_Type::Vertex] = shader_v;
        pso.shaders[RHI_Shader_Type::Pixel]  = shader_p;
        pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Solid);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Alpha);
        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::Off);
        pso.render_target_color_textures[0]  = tex_out;
        pso.clear_color[0]                   = rhi_color_load;

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
