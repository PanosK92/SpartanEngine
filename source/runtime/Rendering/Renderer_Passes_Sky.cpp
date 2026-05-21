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
#include "../World/World.h"
#include "../World/Components/Light.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Shader.h"
//=============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    void Renderer::Pass_Skysphere(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_skysphere                    = GetRenderTarget(Renderer_RenderTarget::skysphere);
        RHI_Texture* tex_lut_atmosphere_transmittance = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_transmittance);
        RHI_Texture* tex_lut_atmosphere_multiscatter  = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_multiscatter);
        RHI_Texture* tex_cloud_noise                  = GetRenderTarget(Renderer_RenderTarget::cloud_noise);

        cmd_list->BeginTimeblock("skysphere");
        {
            if (World::GetDirectionalLight())
            {
                RHI_PipelineState pso;
                pso.name             = "skysphere_atmospheric_scattering";
                pso.shaders[Compute] = GetShader(Renderer_Shader::skysphere_c);
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_skysphere);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_lut_atmosphere_transmittance);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_lut_atmosphere_multiscatter);
                // cloud noise volume reuses the tex3d srv slot, the legacy lut_atmosphere_scatter
                // 3d binding here was dead since the equirectangular kernel never sampled it
                cmd_list->SetTexture(Renderer_BindingsSrv::tex3d, tex_cloud_noise);

                // shader reads buffer_pass via get_camera_position so push constants must be set
                cmd_list->PushConstants(m_pcb_pass_cpu);
                cmd_list->Dispatch(tex_skysphere);
            }
            else
            {
                cmd_list->ClearTexture(tex_skysphere, Color::standard_black);
            }

            // filter all mip levels
            {
                Pass_Downscale(cmd_list, tex_skysphere, Renderer_DownsampleFilter::Average);

                RHI_PipelineState pso;
                pso.name             = "skysphere_filter";
                pso.shaders[Compute] = GetShader(Renderer_Shader::light_integration_environment_filter_c);
                cmd_list->SetPipelineState(pso);
            
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_skysphere);
            
                for (uint32_t mip_level = 1; mip_level < tex_skysphere->GetMipCount(); mip_level++)
                {
                    cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_skysphere, mip_level, 1);
            
                    m_pcb_pass_cpu.set_f3_value(static_cast<float>(mip_level), static_cast<float>(tex_skysphere->GetMipCount()), 0.0f);
                    cmd_list->PushConstants(m_pcb_pass_cpu);
            
                    const uint32_t resolution_x = tex_skysphere->GetWidth() >> mip_level;
                    const uint32_t resolution_y = tex_skysphere->GetHeight() >> mip_level;
                    cmd_list->Dispatch(tex_skysphere);
                    cmd_list->InsertBarrier(tex_skysphere, RHI_BarrierType::EnsureWriteThenRead);
                    // flip the just-written mip back to shader_read so the next iteration's srv sample can read it
                    cmd_list->InsertBarrier(tex_skysphere, RHI_Image_Layout::Shader_Read, mip_level, 1);
                }
            }
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Lut_BrdfSpecular(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_lut_brdf_specular = GetRenderTarget(Renderer_RenderTarget::lut_brdf_specular);

        cmd_list->BeginTimeblock("lut_brdf_specular");
        {
            RHI_PipelineState pso;
            pso.name             = "lut_brdf_specular";
            pso.shaders[Compute] = GetShader(Renderer_Shader::light_integration_brdf_specular_lut_c);
            cmd_list->SetPipelineState(pso);

            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_lut_brdf_specular);
            cmd_list->Dispatch(tex_lut_brdf_specular);

            // permanent srv transition
            cmd_list->InsertBarrier(tex_lut_brdf_specular, RHI_Image_Layout::Shader_Read, 0, 1);
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
            // transmittance lut
            {
                RHI_PipelineState pso;
                pso.name             = "lut_atmosphere_transmittance";
                pso.shaders[Compute] = GetShader(Renderer_Shader::skysphere_transmittance_lut_c);
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_lut_atmosphere_transmittance);
                cmd_list->Dispatch(tex_lut_atmosphere_transmittance);

                tex_lut_atmosphere_transmittance->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            }

            // multi-scatter lut
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

            // legacy 3d lut
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
        // bakes the 3d noise volume sampled by the cloud raymarch inside Pass_Skysphere
        // expensive but only runs once at startup, output stays resident in shader read layout
        RHI_Texture* tex_cloud_noise = GetRenderTarget(Renderer_RenderTarget::cloud_noise);
        if (!tex_cloud_noise)
        {
            return;
        }

        RHI_Shader* shader = GetShader(Renderer_Shader::clouds_noise_c);
        if (!shader || !shader->IsCompiled())
        {
            return;
        }

        cmd_list->BeginTimeblock("cloud_noise");
        {
            RHI_PipelineState pso;
            pso.name             = "cloud_noise";
            pso.shaders[Compute] = shader;
            cmd_list->SetPipelineState(pso);

            cmd_list->SetTexture(Renderer_BindingsUav::tex3d, tex_cloud_noise);
            cmd_list->Dispatch(tex_cloud_noise);

            tex_cloud_noise->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_WindField(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_wind = GetRenderTarget(Renderer_RenderTarget::wind_field);
        if (!tex_wind)
        {
            return;
        }

        RHI_Shader* shader = GetShader(Renderer_Shader::wind_field_c);
        if (!shader || !shader->IsCompiled())
        {
            return;
        }

        cmd_list->BeginTimeblock("wind_field");
        {
            // pre-transition to General before the pipeline bind so SetStandardResources skips
            // the SRV binding for this texture, avoiding a self-conflict during the compute dispatch
            tex_wind->SetLayout(RHI_Image_Layout::General, cmd_list);

            RHI_PipelineState pso;
            pso.name             = "wind_field";
            pso.shaders[Compute] = shader;
            cmd_list->SetPipelineState(pso);

            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_wind);
            cmd_list->Dispatch(tex_wind);

            tex_wind->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        }
        cmd_list->EndTimeblock();
    }
}
