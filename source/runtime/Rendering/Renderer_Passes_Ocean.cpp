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

//= INCLUDES =================================
#include "pch.h"
#include "Renderer.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_Texture.h"
#include "../RHI/RHI_Buffer.h"
#include "../World/World.h"
#include "../World/Components/Water.h"
//===========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        // cpu cache from the oldest completed gpu readback
        vector<Vector4> ocean_heights_cache;
        uint32_t ocean_readback_written_mask = 0;
        array<
            RHI_CommandList*,
            renderer_draw_data_buffer_count
        > ocean_readback_command_lists = {};

        Renderer_Buffer ocean_height_readback_type(
            const uint32_t index
        )
        {
            return static_cast<Renderer_Buffer>(
                static_cast<uint32_t>(
                    Renderer_Buffer::OceanHeightsReadback0
                ) +
                index
            );
        }

        uint32_t ocean_readback_index(
            RHI_CommandList* cmd_list
        )
        {
            for (
                uint32_t i = 0;
                i < renderer_draw_data_buffer_count;
                i++
            )
            {
                if (ocean_readback_command_lists[i] == cmd_list)
                {
                    return i;
                }
            }

            for (
                uint32_t i = 0;
                i < renderer_draw_data_buffer_count;
                i++
            )
            {
                if (!ocean_readback_command_lists[i])
                {
                    ocean_readback_command_lists[i] = cmd_list;
                    return i;
                }
            }

            return numeric_limits<uint32_t>::max();
        }
    }

    void Renderer::Pass_Ocean(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_displacement_a = GetRenderTarget(
            Renderer_RenderTarget::ocean_displacement
        );
        RHI_Texture* tex_displacement_b = GetRenderTarget(
            Renderer_RenderTarget::ocean_displacement_previous
        );
        RHI_Texture* tex_normal = GetRenderTarget(
            Renderer_RenderTarget::ocean_normal
        );
        RHI_Buffer* buffer_heights = GetBuffer(
            Renderer_Buffer::OceanHeights
        );
        if (
            !tex_displacement_a ||
            !tex_displacement_b ||
            !tex_normal ||
            !buffer_heights
        )
        {
            return;
        }

        const Water* water = m_pass_state.ocean;
        if (!water)
        {
            return;
        }

        RHI_Shader* shader_init     = GetShader(Renderer_Shader::ocean_spectrum_init_c);
        RHI_Shader* shader_update   = GetShader(Renderer_Shader::ocean_spectrum_update_c);
        RHI_Shader* shader_fft_h    = GetShader(Renderer_Shader::ocean_fft_horizontal_c);
        RHI_Shader* shader_fft_v    = GetShader(Renderer_Shader::ocean_fft_vertical_c);
        RHI_Shader* shader_assemble = GetShader(Renderer_Shader::ocean_assemble_c);
        const bool shaders_ready =
            shader_init     && shader_init->IsCompiled()     &&
            shader_update   && shader_update->IsCompiled()   &&
            shader_fft_h    && shader_fft_h->IsCompiled()    &&
            shader_fft_v    && shader_fft_v->IsCompiled()    &&
            shader_assemble && shader_assemble->IsCompiled();
        if (!shaders_ready)
        {
            return;
        }

        const uint32_t readback_index =
            ocean_readback_index(cmd_list);
        if (readback_index != numeric_limits<uint32_t>::max())
        {
            ResolveOceanHeightReadback(
                readback_index
            );
        }

        RHI_Texture* tex_spectrum = GetRenderTarget(Renderer_RenderTarget::ocean_spectrum);
        RHI_Texture* tex_fft_a    = GetRenderTarget(Renderer_RenderTarget::ocean_fft_a);
        RHI_Texture* tex_fft_b    = GetRenderTarget(Renderer_RenderTarget::ocean_fft_b);

        const float* lengths = water->GetCascadeLengths();
        const uint32_t n     = renderer_ocean_resolution;
        uint32_t cascades    = water->GetCascadeCount();
        cascades             = cascades < 1 ? 1 : cascades;
        cascades             = cascades > renderer_ocean_max_cascades ? renderer_ocean_max_cascades : cascades;

        // wind comes from the world, re-seed the spectrum whenever it changes
        const Vector3 wind = World::GetWind();
        const float len_xz = sqrtf(
            wind.x * wind.x +
            wind.z * wind.z
        );
        const float wind_speed = len_xz;
        const float dir_x =
            len_xz > 0.0001f ?
            wind.x / len_xz :
            1.0f;
        const float dir_z =
            len_xz > 0.0001f ?
            wind.z / len_xz :
            0.0f;
        if (wind != m_pass_state.ocean_wind)
        {
            m_pass_state.ocean_spectrum_dirty = true;
            m_pass_state.ocean_wind           = wind;
        }

        const bool reset_history =
            m_pass_state.ocean_spectrum_dirty;
        if (reset_history)
        {
            m_pass_state.ocean_displacement_index = 0;
        }

        if (
            !reset_history &&
            m_pass_state.ocean_displacement_produced
        )
        {
            m_pass_state.ocean_displacement_index ^= 1;
            m_pass_state.ocean_displacement_history_valid = true;
        }
        else
        {
            m_pass_state.ocean_displacement_history_valid = false;
        }

        RHI_Texture* tex_displacement =
            m_pass_state.ocean_displacement_index == 0 ?
            tex_displacement_a :
            tex_displacement_b;

        // shared push constants, the cascade lengths are packed across the value slots
        m_pcb_pass_cpu.set_f3_value(dir_x, dir_z, wind_speed);
        m_pcb_pass_cpu.set_f3_value2(lengths[0], lengths[1], lengths[2]);
        m_pcb_pass_cpu.set_f4_value(water->GetAmplitude(), water->GetChoppiness(), water->GetDisplacementScale(), water->GetNormalStrength());
        m_pcb_pass_cpu.set_f2_value(
            lengths[3],
            reset_history ? 1.0f : 0.0f
        );

        cmd_list->BeginTimeblock("ocean");
        {
            // spectrum init, only when the parameters changed
            if (m_pass_state.ocean_spectrum_dirty)
            {
                RHI_PipelineState pso;
                pso.name             = "ocean_spectrum_init";
                pso.shaders[Compute] = shader_init;
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsUav::ocean_spectrum, tex_spectrum);
                cmd_list->PushConstants(m_pcb_pass_cpu);
                cmd_list->Dispatch(n / 8, n / 8, cascades);

                m_pass_state.ocean_spectrum_dirty = false;
            }

            // spectrum update, evolves the spectrum to the current time
            {
                RHI_PipelineState pso;
                pso.name             = "ocean_spectrum_update";
                pso.shaders[Compute] = shader_update;
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsUav::ocean_spectrum, tex_spectrum);
                cmd_list->SetTexture(Renderer_BindingsUav::ocean_fft_a, tex_fft_a);
                cmd_list->SetTexture(Renderer_BindingsUav::ocean_fft_b, tex_fft_b);
                cmd_list->PushConstants(m_pcb_pass_cpu);
                cmd_list->Dispatch(n / 8, n / 8, cascades);
            }

            // inverse fft, one dimension at a time, in place on the working textures
            {
                RHI_PipelineState pso;
                pso.name             = "ocean_fft_horizontal";
                pso.shaders[Compute] = shader_fft_h;
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsUav::ocean_fft_a, tex_fft_a);
                cmd_list->SetTexture(Renderer_BindingsUav::ocean_fft_b, tex_fft_b);
                cmd_list->PushConstants(m_pcb_pass_cpu);
                cmd_list->Dispatch(1, n, cascades);
            }
            {
                RHI_PipelineState pso;
                pso.name             = "ocean_fft_vertical";
                pso.shaders[Compute] = shader_fft_v;
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsUav::ocean_fft_a, tex_fft_a);
                cmd_list->SetTexture(Renderer_BindingsUav::ocean_fft_b, tex_fft_b);
                cmd_list->PushConstants(m_pcb_pass_cpu);
                cmd_list->Dispatch(1, n, cascades);
            }

            // assemble displacement, surface slope and foam from the spatial-domain fields
            {
                RHI_PipelineState pso;
                pso.name             = "ocean_assemble";
                pso.shaders[Compute] = shader_assemble;
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsUav::ocean_fft_a, tex_fft_a);
                cmd_list->SetTexture(Renderer_BindingsUav::ocean_fft_b, tex_fft_b);
                cmd_list->SetTexture(Renderer_BindingsUav::ocean_displacement, tex_displacement);
                cmd_list->SetTexture(Renderer_BindingsUav::ocean_normal, tex_normal);
                cmd_list->PrepareBufferForCompute(
                    buffer_heights
                );
                cmd_list->SetBuffer(
                    Renderer_BindingsUav::ocean_heights,
                    buffer_heights
                );
                cmd_list->PushConstants(m_pcb_pass_cpu);
                cmd_list->Dispatch(n / 8, n / 8, cascades);
            }

            RHI_Buffer* buffer_readback =
                readback_index != numeric_limits<uint32_t>::max() ?
                GetBuffer(
                    ocean_height_readback_type(
                        readback_index
                    )
                ) :
                nullptr;
            if (buffer_readback)
            {
                cmd_list->PrepareBufferForReadback(
                    buffer_heights
                );
                cmd_list->CopyBufferToBuffer(
                    buffer_heights,
                    buffer_readback,
                    buffer_heights->GetObjectSize()
                );
                ocean_readback_written_mask |=
                    1u << readback_index;
            }

            m_pass_state.ocean_displacement_produced = true;
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::ResolveOceanHeightReadback(
        const uint32_t readback_index
    )
    {
        if (
            (
                ocean_readback_written_mask &
                (1u << readback_index)
            ) ==
            0
        )
        {
            return;
        }

        RHI_Buffer* buffer = GetBuffer(
            ocean_height_readback_type(
                readback_index
            )
        );
        if (!buffer || !buffer->GetMappedData())
        {
            return;
        }

        const size_t element_count =
            static_cast<size_t>(
                renderer_ocean_heights_resolution
            ) *
            renderer_ocean_heights_resolution *
            renderer_ocean_max_cascades;
        ocean_heights_cache.resize(element_count);
        memcpy(
            ocean_heights_cache.data(),
            buffer->GetMappedData(),
            buffer->GetObjectSize()
        );
    }

    void Renderer::ResetOceanHeightReadback()
    {
        ocean_heights_cache.clear();
        ocean_readback_written_mask = 0;
        ocean_readback_command_lists.fill(nullptr);
    }

    bool Renderer::GetOceanHeight(const float x, const float z, float& height)
    {
        const Water* water = m_pass_state.ocean;
        if (!water || ocean_heights_cache.empty())
        {
            return false;
        }

        const int n = static_cast<int>(renderer_ocean_heights_resolution);

        // mirror the gpu sampler, uv = world_xz / cascade_length with wrap addressing and bilinear filtering
        const float* lengths = water->GetCascadeLengths();
        uint32_t cascades    = water->GetCascadeCount();
        cascades             = cascades < 1 ? 1 : cascades;
        cascades             = cascades > renderer_ocean_max_cascades ? renderer_ocean_max_cascades : cascades;

        auto sample_displacement =
            [&](const uint32_t cascade, const float sample_x, const float sample_z)
            {
                const Vector4* slice =
                    ocean_heights_cache.data() +
                    cascade * n * n;
                // samples come from full resolution texels zero four eight
                const float fx =
                    sample_x / lengths[cascade] * n -
                    0.125f;
                const float fz =
                    sample_z / lengths[cascade] * n -
                    0.125f;
                const int x0   = static_cast<int>(floorf(fx));
                const int z0   = static_cast<int>(floorf(fz));
                const float tx = fx - x0;
                const float tz = fz - z0;
                const int x0w  = x0 & (n - 1);
                const int x1w  = (x0 + 1) & (n - 1);
                const int z0w  = z0 & (n - 1);
                const int z1w  = (z0 + 1) & (n - 1);

                const Vector4& d00 = slice[z0w * n + x0w];
                const Vector4& d10 = slice[z0w * n + x1w];
                const Vector4& d01 = slice[z1w * n + x0w];
                const Vector4& d11 = slice[z1w * n + x1w];

                auto bilinear =
                    [&](const float v00, const float v10, const float v01, const float v11)
                    {
                        return
                            (v00 * (1.0f - tx) + v10 * tx) *
                            (1.0f - tz) +
                            (v01 * (1.0f - tx) + v11 * tx) *
                            tz;
                    };

                return Vector3(
                    bilinear(d00.x, d10.x, d01.x, d11.x),
                    bilinear(d00.y, d10.y, d01.y, d11.y),
                    bilinear(d00.z, d10.z, d01.z, d11.z)
                );
            };

        Vector2 grid_position(x, z);
        for (uint32_t iteration = 0; iteration < 3; iteration++)
        {
            Vector2 horizontal_displacement = Vector2::Zero;
            for (uint32_t cascade = 0; cascade < cascades; cascade++)
            {
                const Vector3 displacement = sample_displacement(
                    cascade,
                    grid_position.x,
                    grid_position.y
                );
                horizontal_displacement.x += displacement.x;
                horizontal_displacement.y += displacement.z;
            }

            grid_position.x = x - horizontal_displacement.x;
            grid_position.y = z - horizontal_displacement.y;
        }

        height = water->GetSeaLevel();
        for (uint32_t cascade = 0; cascade < cascades; cascade++)
        {
            height += sample_displacement(
                cascade,
                grid_position.x,
                grid_position.y
            ).y;
        }

        return true;
    }
}
