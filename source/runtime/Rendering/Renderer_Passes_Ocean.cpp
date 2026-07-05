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
        // gpu mapped memory is write combined and uncached, scattered reads from it stall for the
        // full memory latency each time, buoyancy samples per body per 200hz substep so it must
        // read a cached ram copy, refreshed with one bulk memcpy per rendered frame
        vector<float> ocean_heights_cache;
        uint64_t ocean_heights_cache_frame = numeric_limits<uint64_t>::max();
    }

    void Renderer::Pass_Ocean(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_displacement = GetRenderTarget(Renderer_RenderTarget::ocean_displacement);
        RHI_Texture* tex_normal       = GetRenderTarget(Renderer_RenderTarget::ocean_normal);
        if (!tex_displacement || !tex_normal)
        {
            return;
        }

        // keep the descriptors valid for the geometry passes that always declare the srvs, even with no water present
        const Water* water = m_pass_state.ocean;
        if (!water)
        {
            tex_displacement->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            tex_normal->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
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
            tex_displacement->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            tex_normal->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            return;
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
        const Vector3 wind     = World::GetWind();
        const float wind_speed = wind.Length();
        const float len_xz     = sqrtf(wind.x * wind.x + wind.z * wind.z);
        const float dir_x      = len_xz > 0.0001f ? wind.x / len_xz : 1.0f;
        const float dir_z      = len_xz > 0.0001f ? wind.z / len_xz : 0.0f;
        if (wind != m_pass_state.ocean_wind)
        {
            m_pass_state.ocean_spectrum_dirty = true;
            m_pass_state.ocean_wind           = wind;
        }

        // shared push constants, the cascade lengths are packed across the value slots
        m_pcb_pass_cpu.set_f3_value(dir_x, dir_z, wind_speed);
        m_pcb_pass_cpu.set_f3_value2(lengths[0], lengths[1], lengths[2]);
        m_pcb_pass_cpu.set_f4_value(water->GetAmplitude(), water->GetChoppiness(), water->GetDisplacementScale(), water->GetNormalStrength());
        m_pcb_pass_cpu.set_f2_value(lengths[3], 0.0f);

        cmd_list->BeginTimeblock("ocean");
        {
            tex_spectrum->SetLayout(RHI_Image_Layout::General, cmd_list);
            tex_fft_a->SetLayout(RHI_Image_Layout::General, cmd_list);
            tex_fft_b->SetLayout(RHI_Image_Layout::General, cmd_list);
            tex_displacement->SetLayout(RHI_Image_Layout::General, cmd_list);
            tex_normal->SetLayout(RHI_Image_Layout::General, cmd_list);

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
                cmd_list->InsertBarrier(tex_spectrum, RHI_BarrierType::EnsureWriteThenRead);

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
                cmd_list->InsertBarrier(tex_fft_a, RHI_BarrierType::EnsureWriteThenRead);
                cmd_list->InsertBarrier(tex_fft_b, RHI_BarrierType::EnsureWriteThenRead);
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
                cmd_list->InsertBarrier(tex_fft_a, RHI_BarrierType::EnsureWriteThenRead);
                cmd_list->InsertBarrier(tex_fft_b, RHI_BarrierType::EnsureWriteThenRead);
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
                cmd_list->InsertBarrier(tex_fft_a, RHI_BarrierType::EnsureWriteThenRead);
                cmd_list->InsertBarrier(tex_fft_b, RHI_BarrierType::EnsureWriteThenRead);
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
                cmd_list->SetBuffer(Renderer_BindingsUav::ocean_heights, GetBuffer(Renderer_Buffer::OceanHeights));
                cmd_list->PushConstants(m_pcb_pass_cpu);
                cmd_list->Dispatch(n / 8, n / 8, cascades);
            }

            tex_displacement->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            tex_normal->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        }
        cmd_list->EndTimeblock();
    }

    bool Renderer::GetOceanHeight(const float x, const float z, float& height)
    {
        const Water* water = m_pass_state.ocean;
        RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::OceanHeights);
        if (!water || !buffer || !buffer->GetMappedData())
        {
            return false;
        }

        // refresh the cached ram copy once per rendered frame, see comment at the top of the file
        const int n = static_cast<int>(renderer_ocean_heights_resolution);
        if (ocean_heights_cache_frame != m_frame_num)
        {
            ocean_heights_cache.resize(static_cast<size_t>(n) * n * renderer_ocean_max_cascades);
            memcpy(ocean_heights_cache.data(), buffer->GetMappedData(), ocean_heights_cache.size() * sizeof(float));
            ocean_heights_cache_frame = m_frame_num;
        }
        const float* heights = ocean_heights_cache.data();

        // mirror the gpu sampler, uv = world_xz / cascade_length with wrap addressing and bilinear filtering
        const float* lengths = water->GetCascadeLengths();
        uint32_t cascades    = water->GetCascadeCount();
        cascades             = cascades > renderer_ocean_max_cascades ? renderer_ocean_max_cascades : cascades;
        height               = water->GetSeaLevel();
        for (uint32_t c = 0; c < cascades; c++)
        {
            const float* slice = heights + c * n * n;
            const float fx     = x / lengths[c] * n - 0.5f;
            const float fz     = z / lengths[c] * n - 0.5f;
            const int x0       = static_cast<int>(floorf(fx));
            const int z0       = static_cast<int>(floorf(fz));
            const float tx     = fx - x0;
            const float tz     = fz - z0;
            const int x0w      = x0 & (n - 1);
            const int x1w      = (x0 + 1) & (n - 1);
            const int z0w      = z0 & (n - 1);
            const int z1w      = (z0 + 1) & (n - 1);
            const float h00    = slice[z0w * n + x0w];
            const float h10    = slice[z0w * n + x1w];
            const float h01    = slice[z1w * n + x0w];
            const float h11    = slice[z1w * n + x1w];
            height            += (h00 * (1.0f - tx) + h10 * tx) * (1.0f - tz) + (h01 * (1.0f - tx) + h11 * tx) * tz;
        }

        return true;
    }
}
