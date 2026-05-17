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
#include "../World/Entity.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Components/AudioSource.h"
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
    void Renderer::Pass_Icons(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (!Engine::IsFlagSet(EngineMode::Playing))
        {
            Vector3 pos_camera = World::GetCamera() ? World::GetCamera()->GetEntity()->GetPosition() : Vector3::Zero;
            for (Entity* entity : World::GetEntities())
            {
                // skip icons too close to camera (erratic screen-space movement)
                if ((entity->GetPosition() - pos_camera).LengthSquared() <= 0.01f)
                {
                    continue;
                }

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
                        RHI_Texture* texture = nullptr;
                        if (light->GetLightType() == LightType::Directional)
                        {
                            texture = GetStandardTexture(Renderer_StandardTexture::Gizmo_light_directional);
                        }
                        else if (light->GetLightType() == LightType::Point)
                        {
                            texture = GetStandardTexture(Renderer_StandardTexture::Gizmo_light_point);
                        }
                        else if (light->GetLightType() == LightType::Spot)
                        {
                            texture = GetStandardTexture(Renderer_StandardTexture::Gizmo_light_spot);
                        }

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
                RHI_PipelineState pso;
                pso.name                              = "icons";
                pso.shaders[RHI_Shader_Type::Compute] = GetShader(Renderer_Shader::icon_c);
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);

                auto dispatch_icon = [&](RHI_Texture* texture, const math::Vector3& pos_world)
                {
                    m_pcb_pass_cpu.set_f3_value(pos_world.x, pos_world.y, pos_world.z);
                    m_pcb_pass_cpu.set_f2_value(static_cast<float>(texture->GetWidth()), static_cast<float>(texture->GetHeight()));
                    cmd_list->PushConstants(m_pcb_pass_cpu);

                    cmd_list->SetTexture(Renderer_BindingsSrv::tex, texture);

                    uint32_t thread_x = 32;
                    uint32_t thread_y = 32;
                    uint32_t groups_x = (texture->GetWidth() + thread_x - 1) / thread_x;
                    uint32_t groups_y = (texture->GetHeight() + thread_y - 1) / thread_y;
                    cmd_list->Dispatch(groups_x, groups_y, 1);

                    // per-icon barrier to avoid out-of-order uav writes on overlapping icons
                    cmd_list->InsertBarrier(tex_out, RHI_BarrierType::EnsureWriteThenRead);
                };
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
        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadGreaterEqual);
        pso.render_target_color_textures[0]  = tex_out;
        pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output);
        cmd_list->SetPipelineState(pso);

        // follow camera in world-unit increments so the grid appears stationary
        {
            const float grid_spacing       = 1.0f;
            const Vector3& camera_position = World::GetCamera()->GetEntity()->GetPosition();
            const Vector3 translation      = Vector3(
                floor(camera_position.x / grid_spacing) * grid_spacing,
                0.0f,
                floor(camera_position.z / grid_spacing) * grid_spacing
            );

            Matrix grid_transform       = Matrix::CreateScale(Vector3(1000.0f, 1.0f, 1000.0f)) * Matrix::CreateTranslation(translation);
            m_pcb_pass_cpu.draw_index = WriteDrawData(grid_transform);
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
            pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadGreaterEqual);
            pso.render_target_color_textures[0]  = tex_out;
            pso.clear_color[0]                   = rhi_color_load;
            pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output);
            pso.primitive_topology               = RHI_PrimitiveTopology::LineList;
            cmd_list->SetPipelineState(pso);

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
                                
                            Render* renderable = entity_selected->GetComponent<Render>();
                            if (!renderable)
                            {
                                continue;
                            }
                                
                            if (!renderable->GetVertexBuffer() || !renderable->GetIndexBuffer())
                            {
                                continue;
                            }

                            m_pcb_pass_cpu.draw_index = WriteDrawData(entity_selected->GetMatrix());
                            m_pcb_pass_cpu.set_f4_value(Color::standard_renderer_lines);
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
