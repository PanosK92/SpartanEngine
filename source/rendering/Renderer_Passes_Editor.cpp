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
#include "Renderer_Internal.h"
#include "../geometry/Mesh.h"
#include "../world/Entity.h"
#include "../world/World.h"
#include "../world/components/Component.h"
#include "../world/components/Camera.h"
#include "../world/components/Light.h"
#include "../world/components/AudioSource.h"
#include "../world/components/ParticleSystem.h"
#include "../world/components/Render.h"
#include "../rhi/RHI_CommandList.h"
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_Shader.h"
#include "../rendering/Material.h"
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
                // mesh plus collider is picked by geometry, a physics icon on the tile
                // center was eating terrain clicks
                if (entry.first == ComponentType::Physics && entity->GetComponent<Render>())
                {
                    continue;
                }

                if (entity->GetComponentByType(entry.first))
                {
                    return Renderer::GetStandardTexture(entry.second);
                }
            }

            return nullptr;
        }
    }

    void Renderer::Pass_Icons(RHI_Texture* tex_out)
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

        RHI_CommandList::BeginPass("icons");
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
                RHI_CommandList::EndPass();
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

            RHI_CommandList::SetShaders(
                GetShader(Renderer_Shader::icon_v),
                GetShader(Renderer_Shader::icon_p)
            );
            RHI_CommandList::SetBlendState(GetBlendState(Renderer_BlendState::Alpha));
            RHI_CommandList::SetColorTarget(tex_out);

            m_pcb_pass_cpu.set_f2_value(
                static_cast<float>(renderer_editor_icon_size_px),
                static_cast<float>(renderer_editor_icon_size_px)
            );
            m_pcb_pass_cpu.set_f4_value(
                static_cast<float>(tex_out->GetWidth()),
                static_cast<float>(tex_out->GetHeight()),
                0.0f,
                0.0f
            );
            RHI_CommandList::SetBufferVertex(m_icons_vertex_buffer.get());
            RHI_CommandList::SetCullMode(RHI_CullMode::None);

            uint32_t vertex_offset = 0;
            RHI_Texture* current_texture = nullptr;
            uint32_t run_icons = 0;

            auto flush_run = [&]()
            {
                if (run_icons == 0 || !current_texture)
                {
                    return;
                }

                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), current_texture);
                RHI_CommandList::Draw(run_icons * 6, vertex_offset);
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

            RHI_CommandList::SetCullMode(RHI_CullMode::Back);
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_Grid(RHI_Texture* tex_out)
    {
        if (!cvar_grid.GetValueAs<bool>())
        {
            return;
        }

        Pass_Graphics(
            "grid",
            Renderer_Shader::grid_v,
            Renderer_Shader::grid_p,
            { tex_out },
            nullptr,
            GetBlendState(Renderer_BlendState::Alpha),
            GetDepthStencilState(Renderer_DepthStencilState::Off),
            nullptr,
            [&]()
            {
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_depth), GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output));

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
                    return;
                }
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);

                RHI_CommandList::SetCullMode(RHI_CullMode::Back);
                RHI_CommandList::SetBufferVertex(GetStandardMesh(MeshType::Quad)->GetVertexBuffer());
                RHI_CommandList::SetBufferIndex(GetStandardMesh(MeshType::Quad)->GetIndexBuffer());
                RHI_CommandList::DrawIndexed(6, GetStandardMesh(MeshType::Quad)->GetGlobalIndexOffset(), GetStandardMesh(MeshType::Quad)->GetGlobalVertexOffset());
            }
        );
    }

    void Renderer::Pass_Lines(RHI_Texture* tex_out)
    {
        RHI_Shader* shader_v  = GetShader(Renderer_Shader::line_v);
        RHI_Shader* shader_p  = GetShader(Renderer_Shader::line_p);
        uint32_t vertex_count = static_cast<uint32_t>(m_lines_vertices.size());

        if (vertex_count != 0)
        {
            RHI_CommandList::BeginPass("lines");
            {
                RHI_CommandList::SetShaders(shader_v, shader_p);
                RHI_CommandList::SetBlendState(GetBlendState(Renderer_BlendState::Alpha));
                RHI_CommandList::SetRasterizerState(GetRasterizerState(Renderer_RasterizerState::Wireframe));
                RHI_CommandList::SetPrimitiveTopology(RHI_PrimitiveTopology::LineList);
                RHI_CommandList::SetColorTarget(tex_out);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_depth), GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output));

                if (vertex_count > m_lines_vertex_buffer->GetElementCount())
                {
                    m_lines_vertex_buffer = make_shared<RHI_Buffer>(
                        RHI_Buffer_Type::Vertex,
                        sizeof(m_lines_vertices[0]),
                        vertex_count,
                        static_cast<void*>(&m_lines_vertices[0]),
                        true,
                        "lines"
                    );
                }

                RHI_Vertex_PosCol* buffer = static_cast<RHI_Vertex_PosCol*>(m_lines_vertex_buffer->GetMappedData());
                memset(buffer, 0, m_lines_vertex_buffer->GetObjectSize());
                copy(m_lines_vertices.begin(), m_lines_vertices.end(), buffer);
                RHI_CommandList::SetBufferVertex(m_lines_vertex_buffer.get());

                RHI_CommandList::SetCullMode(RHI_CullMode::None);
                RHI_CommandList::Draw(static_cast<uint32_t>(m_lines_vertices.size()));
                RHI_CommandList::SetCullMode(RHI_CullMode::Back);
            }
            RHI_CommandList::EndPass();
        }
    }

    void Renderer::Pass_Outline(RHI_Texture* tex_out)
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
                RHI_CommandList::BeginTimeblock("outline");
                {
                    RHI_Texture* tex_outline = GetRenderTarget(Renderer_RenderTarget::outline);

                    // the outline is a selection affordance, but it writes into the draw data buffer that
                    // the world and imgui share, and a subtree can be enormous, selecting a terrain pulls
                    // in its tiles plus every scattered tree and rock, hundreds of thousands of instances,
                    // which exhausts the buffer and drops the editor ui for the rest of the frame
                    const uint32_t outline_draw_budget = 1024;
                    uint32_t outline_draw_count        = 0;
                    bool outline_budget_reached        = false;

                    bool any_rendered = false;
                    RHI_CommandList::BeginPass("color_silhouette");
                    {
                        RHI_CommandList::SetShaders(shader_v, shader_p);
                        RHI_CommandList::SetBlendState(GetBlendState(Renderer_BlendState::Additive));
                        RHI_CommandList::SetClearColor(0, Color::standard_transparent);
                        RHI_CommandList::SetColorTarget(tex_outline);

                        for (Entity* entity_selected : selected_entities)
                        {
                            if (outline_budget_reached)
                            {
                                break;
                            }

                            if (!entity_selected || !entity_selected->GetActive())
                            {
                                continue;
                            }

                            vector<Entity*> to_outline;
                            to_outline.push_back(entity_selected);
                            entity_selected->GetDescendants(&to_outline);

                            for (Entity* entity : to_outline)
                            {
                                if (outline_budget_reached)
                                {
                                    break;
                                }

                                if (!entity || !entity->GetActive())
                                {
                                    continue;
                                }

                                Render* render = entity->GetComponent<Render>();
                                if (!render || !render->GetVertexBuffer() || !render->GetIndexBuffer())
                                {
                                    continue;
                                }

                                // clicking a scattered tree selects the renderable that carries every
                                // tree on that tile, outlining all of them says nothing about which
                                // one was clicked, so a picked instance outlines on its own
                                const int picked = Camera::GetSelectedInstance();
                                // every part of one prop, bark and leaves, shares the tile transform
                                // list, so the same index outlines the whole tree and nothing else
                                const bool one_instance = render->HasInstancing() &&
                                                          picked >= 0 &&
                                                          static_cast<uint32_t>(picked) < render->GetInstanceCount();

                                const uint32_t instance_first = one_instance ? static_cast<uint32_t>(picked) : 0;
                                const uint32_t instance_end   = one_instance
                                    ? static_cast<uint32_t>(picked) + 1
                                    : (render->HasInstancing() ? render->GetInstanceCount() : 1);

                                for (uint32_t i = instance_first; i < instance_end; i++)
                                {
                                    if (outline_draw_count >= outline_draw_budget)
                                    {
                                        outline_budget_reached = true;
                                        break;
                                    }

                                    const Matrix world = render->HasInstancing()
                                        ? render->GetInstance(i, true)
                                        : entity->GetMatrix();
                                    const uint32_t draw_index = WriteDrawData(world);
                                    if (draw_index == numeric_limits<uint32_t>::max())
                                    {
                                        outline_budget_reached = true;
                                        break;
                                    }

                                    outline_draw_count++;

                                    m_pcb_pass_cpu.draw_index = draw_index;
                                    m_pcb_pass_cpu.set_f4_value(Color::standard_renderer_lines);
                                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                                    RHI_CommandList::SetBufferVertex(render->GetVertexBuffer());
                                    RHI_CommandList::SetBufferIndex(render->GetIndexBuffer());
                                    RHI_CommandList::DrawIndexed(
                                        render->GetIndexCount(),
                                        render->GetIndexOffset(),
                                        render->GetVertexOffset()
                                    );
                                    any_rendered = true;
                                }
                            }
                        }
                    }
                    RHI_CommandList::EndPass();

                    if (any_rendered)
                    {
                        {
                            const float radius = 30.0f;
                            Pass_Blur(tex_outline, false, radius);
                        }

                        RHI_CommandList::BeginPass("composition");
                        {
                            RHI_CommandList::SetShader(shader_c);
                            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_out, rhi_all_mips, 0, true);
                            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_outline);
                            RHI_CommandList::Dispatch(tex_out);
                        }
                        RHI_CommandList::EndPass();
                    }
                }
                RHI_CommandList::EndTimeblock();
            }
        }
    }

    void Renderer::Pass_Text(RHI_Texture* tex_out)
    {
        const auto& shader_v  = GetShader(Renderer_Shader::font_v);
        const auto& shader_p  = GetShader(Renderer_Shader::font_p);
        shared_ptr<Font> font = GetFont();

        if (!font->HasText())
        {
            return;
        }

        RHI_CommandList::BeginPass("text");
        {
            font->UpdateVertexAndIndexBuffers();

            RHI_CommandList::SetShaders(shader_v, shader_p);
            RHI_CommandList::SetBlendState(GetBlendState(Renderer_BlendState::Alpha));
            RHI_CommandList::SetColorTarget(tex_out);
            RHI_CommandList::SetBufferVertex(font->GetVertexBuffer());
            RHI_CommandList::SetBufferIndex(font->GetIndexBuffer());
            RHI_CommandList::SetCullMode(RHI_CullMode::Back);

            if (font->GetOutline() != Font_Outline_None && font->GetOutlineSize() != 0)
            {
                m_pcb_pass_cpu.set_f4_value(font->GetColorOutline());
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), font->GetAtlasOutline().get());
                RHI_CommandList::DrawIndexed(font->GetIndexCount());
            }

            {
                m_pcb_pass_cpu.set_f4_value(font->GetColor());
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), font->GetAtlas().get());
                RHI_CommandList::DrawIndexed(font->GetIndexCount());
            }
        }
        RHI_CommandList::EndPass();
    }
}
