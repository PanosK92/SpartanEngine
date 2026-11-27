/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
                { ComponentType::Navigation,     Renderer_StandardTexture::Gizmo_spline          },
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
                const Vector3 pos_camera = Renderer::GetViewCamera() ? Renderer::GetViewCamera()->GetEntity()->GetPosition() : Vector3::Zero;
                const Vector3 cam_forward = Renderer::GetViewCamera() ? Renderer::GetViewCamera()->GetEntity()->GetForward() : Vector3::Forward;

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

        RenderPass pass("icons");
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

            pass.Shaders(Renderer_Shader::icon_v, Renderer_Shader::icon_p);
            pass.Blend(RHI_BlendMode::Alpha);
            pass.ColorTarget(tex_out);
            pass.Set(pass_icon::icon_size, Vector2(static_cast<float>(renderer_editor_icon_size_px), static_cast<float>(renderer_editor_icon_size_px)));
            pass.Set(pass_icon::resolution, Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight())));
            pass.VertexBuffer(m_icons_vertex_buffer.get());
            pass.CullMode(RHI_CullMode::None);

            uint32_t vertex_offset = 0;
            RHI_Texture* current_texture = nullptr;
            uint32_t run_icons = 0;

            auto flush_run = [&]()
            {
                if (run_icons == 0 || !current_texture)
                {
                    return;
                }

                pass.Bind(Renderer_BindingsSrv::tex, current_texture);
                pass.Draw(run_icons * 6, vertex_offset);
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

            pass.CullMode(RHI_CullMode::Back);
        }
    }

    void Renderer::Pass_Grid(RHI_Texture* tex_out)
    {
        if (!cvar_grid.GetValueAs<bool>())
        {
            return;
        }

        RenderPass pass("grid", Renderer_Shader::grid_v, Renderer_Shader::grid_p);
        pass.Blend(RHI_BlendMode::Alpha);
        pass.Depth(RHI_DepthMode::Off);
        pass.Rasterizer(RHI_RasterizerMode::Solid);
        pass.ColorTarget(tex_out);
        pass.Bind(Renderer_BindingsSrv::gbuffer_depth, Renderer_RenderTarget::gbuffer_depth_opaque_output);

        const float grid_spacing       = 1.0f;
        const Vector3& camera_position = Renderer::GetViewCamera()->GetEntity()->GetPosition();
        const Vector3 translation      = Vector3(
            floor(camera_position.x / grid_spacing) * grid_spacing,
            0.0f,
            floor(camera_position.z / grid_spacing) * grid_spacing
        );

        Matrix grid_transform     = Matrix::CreateScale(Vector3(1000.0f, 1.0f, 1000.0f)) * Matrix::CreateTranslation(translation);
        const uint32_t draw_index = WriteDrawData(grid_transform);
        if (draw_index == numeric_limits<uint32_t>::max())
        {
            return;
        }
        pass.DrawIndex(draw_index);
        pass.CullMode(RHI_CullMode::Back);
        pass.VertexBuffer(GetStandardMesh(MeshType::Quad)->GetVertexBuffer());
        pass.IndexBuffer(GetStandardMesh(MeshType::Quad)->GetIndexBuffer());
        pass.DrawIndexed(6, GetStandardMesh(MeshType::Quad)->GetGlobalIndexOffset(), GetStandardMesh(MeshType::Quad)->GetGlobalVertexOffset());
    }

    void Renderer::Pass_DebugTriangles(RHI_Texture* tex_out)
    {
        FrameResource& frame = m_frame_resources[m_frame_resource_index];
        auto& vertex_buffer = frame.debug_triangles_vertex_buffer;
        if (!frame.debug_triangles_uploaded)
        {
            // Upload once per frame slot, shared by both stereo eyes after the GPU fence.
            frame.debug_triangles_vertex_count = static_cast<uint32_t>(m_debug_triangles_vertices.size());
            if (frame.debug_triangles_vertex_count != 0)
            {
                if (!vertex_buffer || frame.debug_triangles_vertex_count > vertex_buffer->GetElementCount())
                    vertex_buffer = make_shared<RHI_Buffer>(RHI_Buffer_Type::Vertex,
                        sizeof(RHI_Vertex_PosCol), frame.debug_triangles_vertex_count, nullptr, true, "debug_triangles");
                memcpy(vertex_buffer->GetMappedData(), m_debug_triangles_vertices.data(),
                    frame.debug_triangles_vertex_count * sizeof(RHI_Vertex_PosCol));
            }
            frame.debug_triangles_uploaded = true;
        }
        if (frame.debug_triangles_vertex_count == 0) return;

        // The line shader also handles solid vertex colours and rejects fragments behind
        // scene depth. Alpha blend over the scene without writing to its depth buffer.
        RenderPass pass("debug_triangles", Renderer_Shader::line_v, Renderer_Shader::line_p);
        pass.Blend(RHI_BlendMode::Alpha);
        pass.Depth(RHI_DepthMode::Off);
        pass.Rasterizer(RHI_RasterizerMode::Solid);
        pass.Topology(RHI_PrimitiveTopology::TriangleList);
        pass.ColorTarget(tex_out);
        pass.Bind(Renderer_BindingsSrv::gbuffer_depth, Renderer_RenderTarget::gbuffer_depth_opaque_output);
        pass.VertexBuffer(vertex_buffer.get());
        pass.CullMode(RHI_CullMode::None);
        pass.Draw(frame.debug_triangles_vertex_count);
        pass.CullMode(RHI_CullMode::Back);
    }

    void Renderer::Pass_Lines(RHI_Texture* tex_out)
    {
        RHI_Shader* shader_v  = GetShader(Renderer_Shader::line_v);
        RHI_Shader* shader_p  = GetShader(Renderer_Shader::line_p);
        FrameResource& frame = m_frame_resources[m_frame_resource_index];
        auto& vertex_buffer = frame.lines_vertex_buffer;
        if (!frame.lines_uploaded)
        {
            // RotateFrameBuffers waited for this slot's previous GPU submission.
            // Upload once: both stereo eyes must read the same immutable vertices.
            frame.lines_vertex_count = static_cast<uint32_t>(m_lines_vertices.size());
            if (frame.lines_vertex_count != 0)
            {
                if (!vertex_buffer || frame.lines_vertex_count > vertex_buffer->GetElementCount())
                {
                    vertex_buffer = make_shared<RHI_Buffer>(RHI_Buffer_Type::Vertex,
                        sizeof(RHI_Vertex_PosCol), frame.lines_vertex_count, nullptr, true, "lines");
                }
                memcpy(vertex_buffer->GetMappedData(), m_lines_vertices.data(),
                    frame.lines_vertex_count * sizeof(RHI_Vertex_PosCol));
            }
            frame.lines_uploaded = true;
        }
        const uint32_t vertex_count = frame.lines_vertex_count;

        if (vertex_count != 0)
        {
            RenderPass pass("lines", shader_v, shader_p);
            pass.Blend(RHI_BlendMode::Alpha);
            pass.Rasterizer(RHI_RasterizerMode::Wireframe);
            pass.Topology(RHI_PrimitiveTopology::LineList);
            pass.ColorTarget(tex_out);
            pass.Bind(Renderer_BindingsSrv::gbuffer_depth, Renderer_RenderTarget::gbuffer_depth_opaque_output);
            pass.VertexBuffer(vertex_buffer.get());
            pass.CullMode(RHI_CullMode::None);
            pass.Draw(vertex_count);
            pass.CullMode(RHI_CullMode::Back);
        }
    }

    void Renderer::Pass_Outline(RHI_Texture* tex_out)
    {
        // while playing the editor selection is hidden and the outline marks what gameplay points at instead
        const bool playing = Engine::IsFlagSet(EngineMode::Playing);
        if (!playing && !cvar_selection_outline.GetValueAs<bool>())
        {
            return;
        }

        RHI_Shader* shader_v = GetShader(Renderer_Shader::outline_v);
        RHI_Shader* shader_p = GetShader(Renderer_Shader::outline_p);
        RHI_Shader* shader_c = GetShader(Renderer_Shader::outline_c);

        if (Camera* camera = Renderer::GetViewCamera())
        {
            std::vector<Entity*> selected_entities;
            if (!playing)
            {
                selected_entities = GetEditorSelection();
            }
            else if (Entity* highlight = GetGameplayHighlight())
            {
                selected_entities.push_back(highlight);
            }
            const Color outline_color = playing ? Color(1.0f, 0.86f, 0.62f, 1.0f) : Color::standard_renderer_lines;
            if (!selected_entities.empty())
            {
                RenderPass pass_outline("outline");
                RHI_Texture* tex_outline = GetRenderTarget(Renderer_RenderTarget::outline);

                // the outline is a selection affordance, but it writes into the draw data buffer that
                // the world and imgui share, and a subtree can be enormous, selecting a terrain pulls
                // in its tiles plus every scattered tree and rock, hundreds of thousands of instances,
                // which exhausts the buffer and drops the editor ui for the rest of the frame
                const uint32_t outline_draw_budget = 1024;
                uint32_t outline_draw_count        = 0;
                bool outline_budget_reached        = false;

                bool any_rendered = false;
                {
                    RenderPass pass("color_silhouette", shader_v, shader_p);
                    pass.Blend(RHI_BlendMode::Additive);
                    pass.ColorTarget(tex_outline, Color::standard_transparent);
                    pass.Set(pass_outline::color, outline_color);

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
                            const int picked = playing ? -1 : GetEditorSelectedInstance();
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

                                pass.DrawIndex(draw_index);
                                pass.VertexBuffer(render->GetVertexBuffer());
                                pass.IndexBuffer(render->GetIndexBuffer());
                                pass.DrawIndexed(
                                    render->GetIndexCount(),
                                    render->GetIndexOffset(),
                                    render->GetVertexOffset()
                                );
                                any_rendered = true;
                            }
                        }
                    }
                }

                if (any_rendered)
                {
                    {
                        const float radius = 30.0f;
                        Pass_Blur(tex_outline, false, radius);
                    }

                    RenderPass pass("composition", shader_c);
                    pass.Bind(Renderer_BindingsUav::tex, tex_out);
                    pass.Bind(Renderer_BindingsSrv::tex, tex_outline);
                    pass.Dispatch(tex_out);
                }
            }
        }
    }

    void Renderer::Pass_Text(RHI_Texture* tex_out, const bool display_encoded, const bool cinematic_only)
    {
        if (!tex_out || !tex_out->GetRhiResource())
        {
            return;
        }

        const auto& shader_v = GetShader(Renderer_Shader::font_v);
        const auto& shader_p = GetShader(Renderer_Shader::font_p);

        const uint32_t font_first = cinematic_only ? static_cast<uint32_t>(Renderer_Font::CinematicSmall) : 0;
        bool has_text = false;
        for (uint32_t i = font_first; i < static_cast<uint32_t>(Renderer_Font::Max); i++)
        {
            const shared_ptr<Font>& font = GetFont(static_cast<Renderer_Font>(i));
            has_text |= font && font->HasText();
        }

        if (!has_text)
        {
            return;
        }

        // upload first, the backdrop rectangles are published together with the text
        for (uint32_t i = font_first; i < static_cast<uint32_t>(Renderer_Font::Max); i++)
        {
            const shared_ptr<Font>& font = GetFont(static_cast<Renderer_Font>(i));
            if (font && font->HasText())
            {
                font->UpdateVertexAndIndexBuffers();
            }
        }

        // frosted glass behind overlay panels, the frame is blurred once at quarter resolution and copied under every panel
        RHI_Texture* tex_backdrop     = GetRenderTarget(Renderer_RenderTarget::overlay_backdrop);
        RHI_Texture* tex_blur         = GetRenderTarget(Renderer_RenderTarget::blur);
        RHI_Shader* shader_composite  = GetShader(Renderer_Shader::blur_composite_c);
        const bool backdrop_supported =
            !cinematic_only && tex_backdrop && tex_backdrop->GetRhiResource() && tex_blur && tex_blur->GetRhiResource() && shader_composite && shader_composite->IsCompiled() &&
            tex_blur->GetWidth() >= tex_backdrop->GetWidth() && tex_blur->GetHeight() >= tex_backdrop->GetHeight();
        if (backdrop_supported)
        {
            const float scale_x = static_cast<float>(tex_out->GetWidth()) / max(GetViewport().width, 1.0f);
            const float scale_y = static_cast<float>(tex_out->GetHeight()) / max(GetViewport().height, 1.0f);
            bool captured       = false;
            for (uint32_t i = font_first; i < static_cast<uint32_t>(Renderer_Font::Max); i++)
            {
                const shared_ptr<Font>& font = GetFont(static_cast<Renderer_Font>(i));
                if (!font)
                {
                    continue;
                }

                for (const math::Rectangle& rect : font->GetBackdropBlurs())
                {
                    if (!captured)
                    {
                        RHI_CommandList::Blit(tex_out, tex_backdrop, false);
                        Pass_Blur(tex_backdrop, false, 10.0f);
                        Pass_Blur(tex_backdrop, false, 10.0f);
                        captured = true;
                    }

                    const float x0 = floor(rect.x * scale_x);
                    const float y0 = floor(rect.y * scale_y);
                    const float x1 = ceil((rect.x + rect.width) * scale_x);
                    const float y1 = ceil((rect.y + rect.height) * scale_y);

                    RenderPass pass("text_backdrop", shader_composite, false);
                    pass.Set(pass_blur::rect_x0, x0);
                    pass.Set(pass_blur::rect_y0, y0);
                    pass.Set(pass_blur::rect_x1, x1);
                    pass.Set(pass_blur::rect_y1, y1);
                    pass.Bind(Renderer_BindingsSrv::tex, tex_backdrop);
                    pass.Bind(Renderer_BindingsUav::tex, tex_out);
                    pass.Dispatch((static_cast<uint32_t>(x1 - x0) + 7) / 8, (static_cast<uint32_t>(y1 - y0) + 7) / 8);
                }
            }
        }

        RenderPass pass("text", shader_v, shader_p);
        pass.Blend(RHI_BlendMode::Alpha);
        pass.ColorTarget(tex_out);
        pass.CullMode(RHI_CullMode::Back);
        pass.Set(pass_font::display_encoded, display_encoded);

        for (uint32_t i = font_first; i < static_cast<uint32_t>(Renderer_Font::Max); i++)
        {
            const shared_ptr<Font>& font = GetFont(static_cast<Renderer_Font>(i));
            if (!font || !font->HasText())
            {
                continue;
            }

            pass.VertexBuffer(font->GetVertexBuffer());
            pass.IndexBuffer(font->GetIndexBuffer());

            if (font->GetOutline() != Font_Outline_None && font->GetOutlineSize() != 0 && font->GetAtlasOutline())
            {
                Color outline = font->GetColorOutline();
                outline.a     = 1.0f;
                pass.Set(pass_font::outline_color, outline);
                pass.Bind(Renderer_BindingsSrv::tex, font->GetAtlasOutline().get());
                pass.DrawIndexed(font->GetIndexCount());
            }

            // zero alpha tells the shader to use the per vertex color
            pass.Set(pass_font::outline_color, Vector4::Zero);
            pass.Bind(Renderer_BindingsSrv::tex, font->GetAtlas().get());
            pass.DrawIndexed(font->GetIndexCount());
        }
    }
}
