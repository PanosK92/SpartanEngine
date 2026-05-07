/*
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

//= INCLUDES ========================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Pipeline.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_Shader.h"
#include "../RHI_BlendState.h"
#include "../RHI_DepthStencilState.h"
#include "../RHI_InputLayout.h"
#include "../RHI_SwapChain.h"
#include "../RHI_Texture.h"
#include "../Rendering/Renderer.h"
#include <cstring>
//===================================

using namespace std;

namespace spartan
{
    // forward declarations
    static void create_root_signature_imgui(RHI_Pipeline* pipeline);
    static void create_root_signature_bindless(RHI_Pipeline* pipeline);
    static void create_compute_pipeline(RHI_Pipeline* pipeline, RHI_PipelineState& state);
    static void create_graphics_pipeline(RHI_Pipeline* pipeline, RHI_PipelineState& state);

    static bool pso_is_imgui(const RHI_PipelineState& state)
    {
        return state.name != nullptr && strcmp(state.name, "imgui") == 0;
    }

    RHI_Pipeline::RHI_Pipeline(RHI_PipelineState& pipeline_state, RHI_DescriptorSetLayout* descriptor_set_layout)
    {
        m_state = pipeline_state;

        if (pipeline_state.IsCompute())
        {
            create_compute_pipeline(this, m_state);
        }
        else if (pipeline_state.IsGraphics())
        {
            create_graphics_pipeline(this, m_state);
        }
    }

    static void create_compute_pipeline(RHI_Pipeline* pipeline, RHI_PipelineState& state)
    {
        // compute always uses the bindless root signature
        create_root_signature_bindless(pipeline);

        if (!pipeline->GetRhiResourceLayout())
        {
            SP_LOG_ERROR("Failed to create root signature for compute pipeline '%s'", state.name ? state.name : "?");
            return;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = static_cast<ID3D12RootSignature*>(pipeline->GetRhiResourceLayout());

        if (state.shaders[RHI_Shader_Type::Compute])
        {
            desc.CS.pShaderBytecode = state.shaders[RHI_Shader_Type::Compute]->GetRhiResource();
            desc.CS.BytecodeLength  = state.shaders[RHI_Shader_Type::Compute]->GetObjectSize();
        }

        void* resource = nullptr;
        HRESULT hr = RHI_Context::device->CreateComputePipelineState(&desc, IID_PPV_ARGS(reinterpret_cast<ID3D12PipelineState**>(&resource)));
        if (FAILED(hr))
        {
            SP_LOG_ERROR("Failed to create compute pipeline state '%s': %s", state.name ? state.name : "?", d3d12_utility::error::dxgi_error_to_string(hr));
        }
        pipeline->SetRhiResource(resource);
    }

    static void create_graphics_pipeline(RHI_Pipeline* pipeline, RHI_PipelineState& state)
    {
        // choose root sig based on pso type
        if (pso_is_imgui(state))
        {
            create_root_signature_imgui(pipeline);
        }
        else
        {
            create_root_signature_bindless(pipeline);
        }

        if (!pipeline->GetRhiResourceLayout())
        {
            SP_LOG_ERROR("Failed to create root signature for graphics pipeline '%s'", state.name ? state.name : "?");
            return;
        }

        // rasterizer description
        D3D12_RASTERIZER_DESC desc_rasterizer = {};
        if (state.rasterizer_state)
        {
            desc_rasterizer.FillMode              = d3d12_polygon_mode[static_cast<uint32_t>(state.rasterizer_state->GetPolygonMode())];
            desc_rasterizer.CullMode              = pso_is_imgui(state) ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
            desc_rasterizer.FrontCounterClockwise = false;
            desc_rasterizer.DepthBias             = static_cast<INT>(state.rasterizer_state->GetDepthBias());
            desc_rasterizer.DepthBiasClamp        = state.rasterizer_state->GetDepthBiasClamp();
            desc_rasterizer.SlopeScaledDepthBias  = state.rasterizer_state->GetDepthBiasSlopeScaled();
            desc_rasterizer.DepthClipEnable       = state.rasterizer_state->GetDepthClipEnabled();
        }
        else
        {
            desc_rasterizer.FillMode        = D3D12_FILL_MODE_SOLID;
            desc_rasterizer.CullMode        = D3D12_CULL_MODE_NONE;
            desc_rasterizer.DepthClipEnable = true;
        }

        // for hdr swapchains (r10g10b10a2_unorm), dwm uses the alpha channel during compositing,
        // imgui's standard alpha blending overwrites alpha to 0 which makes the window transparent on hdr displays,
        // so for the imgui pso we strip alpha out of the write mask to keep the cleared alpha value (1)
        const UINT8 imgui_write_mask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
        const UINT8 write_mask       = pso_is_imgui(state) ? imgui_write_mask : D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_BLEND_DESC desc_blend_state = {};
        if (state.blend_state)
        {
            desc_blend_state.RenderTarget[0].BlendEnable           = state.blend_state->GetBlendEnabled();
            desc_blend_state.RenderTarget[0].SrcBlend              = d3d12_blend_factor[static_cast<uint32_t>(state.blend_state->GetSourceBlend())];
            desc_blend_state.RenderTarget[0].DestBlend             = d3d12_blend_factor[static_cast<uint32_t>(state.blend_state->GetDestBlend())];
            desc_blend_state.RenderTarget[0].BlendOp               = d3d12_blend_operation[static_cast<uint32_t>(state.blend_state->GetBlendOp())];
            desc_blend_state.RenderTarget[0].SrcBlendAlpha         = d3d12_blend_factor[static_cast<uint32_t>(state.blend_state->GetSourceBlendAlpha())];
            desc_blend_state.RenderTarget[0].DestBlendAlpha        = d3d12_blend_factor[static_cast<uint32_t>(state.blend_state->GetDestBlendAlpha())];
            desc_blend_state.RenderTarget[0].BlendOpAlpha          = d3d12_blend_operation[static_cast<uint32_t>(state.blend_state->GetBlendOpAlpha())];
            desc_blend_state.RenderTarget[0].RenderTargetWriteMask = write_mask;
        }
        else
        {
            desc_blend_state.RenderTarget[0].BlendEnable           = false;
            desc_blend_state.RenderTarget[0].RenderTargetWriteMask = write_mask;
        }

        // depth-stencil state
        D3D12_DEPTH_STENCIL_DESC desc_depth_stencil_state = {};
        if (state.depth_stencil_state)
        {
            desc_depth_stencil_state.DepthEnable                  = state.depth_stencil_state->GetDepthTestEnabled();
            desc_depth_stencil_state.DepthWriteMask               = state.depth_stencil_state->GetDepthWriteEnabled() ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            desc_depth_stencil_state.DepthFunc                    = d3d12_comparison_function[static_cast<uint32_t>(state.depth_stencil_state->GetDepthComparisonFunction())];
            desc_depth_stencil_state.StencilEnable                = static_cast<BOOL>(state.depth_stencil_state->GetStencilTestEnabled() || state.depth_stencil_state->GetStencilWriteEnabled());
            desc_depth_stencil_state.StencilReadMask              = state.depth_stencil_state->GetStencilReadMask();
            desc_depth_stencil_state.StencilWriteMask             = state.depth_stencil_state->GetStencilWriteMask();
            desc_depth_stencil_state.FrontFace.StencilFailOp      = d3d12_stencil_operation[static_cast<uint32_t>(state.depth_stencil_state->GetStencilFailOperation())];
            desc_depth_stencil_state.FrontFace.StencilDepthFailOp = d3d12_stencil_operation[static_cast<uint32_t>(state.depth_stencil_state->GetStencilDepthFailOperation())];
            desc_depth_stencil_state.FrontFace.StencilPassOp      = d3d12_stencil_operation[static_cast<uint32_t>(state.depth_stencil_state->GetStencilPassOperation())];
            desc_depth_stencil_state.FrontFace.StencilFunc        = d3d12_comparison_function[static_cast<uint32_t>(state.depth_stencil_state->GetStencilComparisonFunction())];
            desc_depth_stencil_state.BackFace                     = desc_depth_stencil_state.FrontFace;
        }
        else
        {
            desc_depth_stencil_state.DepthEnable    = false;
            desc_depth_stencil_state.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc_depth_stencil_state.StencilEnable  = false;
        }

        // input layout
        D3D12_INPUT_LAYOUT_DESC desc_input_layout = {};
        vector<D3D12_INPUT_ELEMENT_DESC> vertex_attributes;
        RHI_Shader* shader_vertex = state.shaders[RHI_Shader_Type::Vertex];
        if (shader_vertex)
        {
            if (RHI_InputLayout* input_layout = shader_vertex->GetInputLayout().get())
            {
                vertex_attributes.reserve(input_layout->GetAttributeDescriptions().size());

                for (const VertexAttribute& attribute : input_layout->GetAttributeDescriptions())
                {
                    vertex_attributes.push_back
                    ({
                        attribute.name.c_str(),
                        0,
                        d3d12_format[rhi_format_to_index(attribute.format)],
                        0,
                        attribute.offset,
                        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                        0
                    });
                }
            }

            desc_input_layout.pInputElementDescs = vertex_attributes.data();
            desc_input_layout.NumElements        = static_cast<uint32_t>(vertex_attributes.size());
        }

        // determine render target formats from pso (supports mrt)
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        uint32_t rt_count = 0;
        for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
        {
            if (state.render_target_color_textures[i])
            {
                desc.RTVFormats[i] = d3d12_format[rhi_format_to_index(state.render_target_color_textures[i]->GetFormat())];
                rt_count           = i + 1;
            }
        }
        if (rt_count == 0 && state.render_target_swapchain)
        {
            desc.RTVFormats[0] = d3d12_format[rhi_format_to_index(state.render_target_swapchain->GetFormat())];
            rt_count           = 1;
        }
        desc.NumRenderTargets = rt_count;

        // depth format
        if (state.render_target_depth_texture)
        {
            DXGI_FORMAT dsv = DXGI_FORMAT_D32_FLOAT;
            switch (state.render_target_depth_texture->GetFormat())
            {
                case RHI_Format::D16_Unorm:             dsv = DXGI_FORMAT_D16_UNORM; break;
                case RHI_Format::D32_Float:             dsv = DXGI_FORMAT_D32_FLOAT; break;
                case RHI_Format::D32_Float_S8X24_Uint:  dsv = DXGI_FORMAT_D32_FLOAT_S8X24_UINT; break;
                default: break;
            }
            desc.DSVFormat = dsv;
        }

        desc.InputLayout           = desc_input_layout;
        desc.pRootSignature        = static_cast<ID3D12RootSignature*>(pipeline->GetRhiResourceLayout());
        desc.RasterizerState       = desc_rasterizer;
        desc.BlendState            = desc_blend_state;
        desc.DepthStencilState     = desc_depth_stencil_state;
        desc.SampleMask            = UINT_MAX;
        desc.PrimitiveTopologyType = (state.primitive_toplogy == RHI_PrimitiveTopology::LineList) ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.SampleDesc.Count      = 1;

        if (state.shaders[RHI_Shader_Type::Vertex])
        {
            desc.VS.pShaderBytecode = state.shaders[RHI_Shader_Type::Vertex]->GetRhiResource();
            desc.VS.BytecodeLength  = state.shaders[RHI_Shader_Type::Vertex]->GetObjectSize();
        }
        if (state.shaders[RHI_Shader_Type::Pixel])
        {
            desc.PS.pShaderBytecode = state.shaders[RHI_Shader_Type::Pixel]->GetRhiResource();
            desc.PS.BytecodeLength  = state.shaders[RHI_Shader_Type::Pixel]->GetObjectSize();
        }

        void* resource = nullptr;
        HRESULT hr = RHI_Context::device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(reinterpret_cast<ID3D12PipelineState**>(&resource)));
        if (FAILED(hr))
        {
            SP_LOG_ERROR("Failed to create graphics pipeline state '%s': %s", state.name ? state.name : "?", d3d12_utility::error::dxgi_error_to_string(hr));
        }
        pipeline->SetRhiResource(resource);
    }

    // imgui root signature - simple, matches the simplified imgui.hlsl d3d12 path
    // param 0: root constants at b0 (transform matrix, 16 dwords)
    // param 1: descriptor table - srv t0 (font texture)
    // static sampler at s0
    static void create_root_signature_imgui(RHI_Pipeline* pipeline)
    {
        D3D12_ROOT_PARAMETER root_params[2] = {};

        root_params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_params[0].Constants.ShaderRegister = 0;
        root_params[0].Constants.RegisterSpace  = 0;
        root_params[0].Constants.Num32BitValues = 32;
        root_params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE srv_range = {};
        srv_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors                    = 1;
        srv_range.BaseShaderRegister                = 0;
        srv_range.RegisterSpace                     = 0;
        srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        root_params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_params[1].DescriptorTable.NumDescriptorRanges = 1;
        root_params[1].DescriptorTable.pDescriptorRanges   = &srv_range;
        root_params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler_desc = {};
        sampler_desc.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_desc.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler_desc.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler_desc.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler_desc.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER; // non-comparison sampler, validation warns on anything other than never
        sampler_desc.MinLOD           = 0.0f;
        sampler_desc.MaxLOD           = D3D12_FLOAT32_MAX;
        sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {};
        root_sig_desc.NumParameters     = 2;
        root_sig_desc.pParameters       = root_params;
        root_sig_desc.NumStaticSamplers = 1;
        root_sig_desc.pStaticSamplers   = &sampler_desc;
        root_sig_desc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* signature_blob = nullptr;
        ID3DBlob* error_blob     = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error_blob);
        if (FAILED(hr))
        {
            if (error_blob)
            {
                SP_LOG_ERROR("Failed to serialize imgui root signature: %s", static_cast<char*>(error_blob->GetBufferPointer()));
                error_blob->Release();
            }
            if (signature_blob) signature_blob->Release();
            return;
        }

        void* layout = nullptr;
        RHI_Context::device->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(),
            IID_PPV_ARGS(reinterpret_cast<ID3D12RootSignature**>(&layout)));
        pipeline->SetRhiResourceLayout(layout);

        if (signature_blob) signature_blob->Release();
        if (error_blob) error_blob->Release();
    }

    // unified bindless root signature matching common_resources.hlsl layout
    // root parameter slots (see D3D12_RootSlot below):
    //   0: CBV b0 space0 (buffer_frame)
    //   1: 32-bit root constants b1 space0 (buffer_pass - 16 dwords = 64 bytes)
    //   2: SRV table t0..t26 space0
    //   3: UAV table u0..u42 space0
    //   4: SRV table t15 space1 unbounded (material_textures[])
    //   5: SRV table t16 space2 (material_parameters)
    //   6: SRV table t17 space3 (light_parameters)
    //   7: SRV table t18 space4 (aabbs)
    //   8: SRV table t19 space5 (draw_data)
    //   9: SRV table t20 space8 (geometry_vertices) + t22 space9 (indices) + t23 space10 (instances)
    //  10: Sampler table s0 space6 unbounded + s1 space7 unbounded
    static void create_root_signature_bindless(RHI_Pipeline* pipeline)
    {
        constexpr uint32_t param_count = 11;
        D3D12_ROOT_PARAMETER params[param_count] = {};

        // 0: CBV b0 (buffer_frame)
        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].Descriptor.RegisterSpace  = 0;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        // 1: 32-bit constants b1 (buffer_pass)
        params[1].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants.ShaderRegister = 1;
        params[1].Constants.RegisterSpace  = 0;
        params[1].Constants.Num32BitValues = 16; // PassBufferData = 64 bytes
        params[1].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;

        // 2: SRV table t0..t26 space0
        static D3D12_DESCRIPTOR_RANGE srv0_range = {};
        srv0_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv0_range.NumDescriptors                    = 27;
        srv0_range.BaseShaderRegister                = 0;
        srv0_range.RegisterSpace                     = 0;
        srv0_range.OffsetInDescriptorsFromTableStart = 0;
        params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.NumDescriptorRanges = 1;
        params[2].DescriptorTable.pDescriptorRanges   = &srv0_range;
        params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        // 3: UAV table u0..u42 space0
        static D3D12_DESCRIPTOR_RANGE uav0_range = {};
        uav0_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uav0_range.NumDescriptors                    = 43;
        uav0_range.BaseShaderRegister                = 0;
        uav0_range.RegisterSpace                     = 0;
        uav0_range.OffsetInDescriptorsFromTableStart = 0;
        params[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[3].DescriptorTable.NumDescriptorRanges = 1;
        params[3].DescriptorTable.pDescriptorRanges   = &uav0_range;
        params[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        // 4: SRV table t15 space1 (material_textures[], unbounded)
        static D3D12_DESCRIPTOR_RANGE mat_tex_range = {};
        mat_tex_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        mat_tex_range.NumDescriptors                    = UINT_MAX; // unbounded
        mat_tex_range.BaseShaderRegister                = 15;
        mat_tex_range.RegisterSpace                     = 1;
        mat_tex_range.OffsetInDescriptorsFromTableStart = 0;
        params[4].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[4].DescriptorTable.NumDescriptorRanges = 1;
        params[4].DescriptorTable.pDescriptorRanges   = &mat_tex_range;
        params[4].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        // 5: SRV table t16 space2 (material_parameters)
        static D3D12_DESCRIPTOR_RANGE mat_param_range = {};
        mat_param_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        mat_param_range.NumDescriptors                    = 1;
        mat_param_range.BaseShaderRegister                = 16;
        mat_param_range.RegisterSpace                     = 2;
        mat_param_range.OffsetInDescriptorsFromTableStart = 0;
        params[5].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[5].DescriptorTable.NumDescriptorRanges = 1;
        params[5].DescriptorTable.pDescriptorRanges   = &mat_param_range;
        params[5].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        // 6: SRV table t17 space3 (light_parameters)
        static D3D12_DESCRIPTOR_RANGE light_range = {};
        light_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        light_range.NumDescriptors                    = 1;
        light_range.BaseShaderRegister                = 17;
        light_range.RegisterSpace                     = 3;
        light_range.OffsetInDescriptorsFromTableStart = 0;
        params[6].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[6].DescriptorTable.NumDescriptorRanges = 1;
        params[6].DescriptorTable.pDescriptorRanges   = &light_range;
        params[6].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        // 7: SRV table t18 space4 (aabbs)
        static D3D12_DESCRIPTOR_RANGE aabb_range = {};
        aabb_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        aabb_range.NumDescriptors                    = 1;
        aabb_range.BaseShaderRegister                = 18;
        aabb_range.RegisterSpace                     = 4;
        aabb_range.OffsetInDescriptorsFromTableStart = 0;
        params[7].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[7].DescriptorTable.NumDescriptorRanges = 1;
        params[7].DescriptorTable.pDescriptorRanges   = &aabb_range;
        params[7].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        // 8: SRV table t19 space5 (draw_data)
        static D3D12_DESCRIPTOR_RANGE draw_range = {};
        draw_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        draw_range.NumDescriptors                    = 1;
        draw_range.BaseShaderRegister                = 19;
        draw_range.RegisterSpace                     = 5;
        draw_range.OffsetInDescriptorsFromTableStart = 0;
        params[8].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[8].DescriptorTable.NumDescriptorRanges = 1;
        params[8].DescriptorTable.pDescriptorRanges   = &draw_range;
        params[8].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        // 9: SRV table - geometry vertices/indices/instances in separate spaces (3 ranges)
        static D3D12_DESCRIPTOR_RANGE geo_ranges[3] = {};
        geo_ranges[0].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        geo_ranges[0].NumDescriptors                    = 1;
        geo_ranges[0].BaseShaderRegister                = 20;
        geo_ranges[0].RegisterSpace                     = 8;
        geo_ranges[0].OffsetInDescriptorsFromTableStart = 0;
        geo_ranges[1].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        geo_ranges[1].NumDescriptors                    = 1;
        geo_ranges[1].BaseShaderRegister                = 22;
        geo_ranges[1].RegisterSpace                     = 9;
        geo_ranges[1].OffsetInDescriptorsFromTableStart = 1;
        geo_ranges[2].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        geo_ranges[2].NumDescriptors                    = 1;
        geo_ranges[2].BaseShaderRegister                = 23;
        geo_ranges[2].RegisterSpace                     = 10;
        geo_ranges[2].OffsetInDescriptorsFromTableStart = 2;
        params[9].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[9].DescriptorTable.NumDescriptorRanges = 3;
        params[9].DescriptorTable.pDescriptorRanges   = geo_ranges;
        params[9].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        // 10: Sampler table s0 space6 unbounded + s1 space7 unbounded
        static D3D12_DESCRIPTOR_RANGE sampler_ranges[2] = {};
        sampler_ranges[0].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        sampler_ranges[0].NumDescriptors                    = UINT_MAX;
        sampler_ranges[0].BaseShaderRegister                = 0;
        sampler_ranges[0].RegisterSpace                     = 6;
        sampler_ranges[0].OffsetInDescriptorsFromTableStart = 0;
        sampler_ranges[1].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        sampler_ranges[1].NumDescriptors                    = UINT_MAX;
        sampler_ranges[1].BaseShaderRegister                = 1;
        sampler_ranges[1].RegisterSpace                     = 7;
        sampler_ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        params[10].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[10].DescriptorTable.NumDescriptorRanges = 2;
        params[10].DescriptorTable.pDescriptorRanges   = sampler_ranges;
        params[10].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {};
        root_sig_desc.NumParameters     = param_count;
        root_sig_desc.pParameters       = params;
        root_sig_desc.NumStaticSamplers = 0;
        root_sig_desc.pStaticSamplers   = nullptr;
        root_sig_desc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* signature_blob = nullptr;
        ID3DBlob* error_blob     = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error_blob);
        if (FAILED(hr))
        {
            if (error_blob)
            {
                SP_LOG_ERROR("Failed to serialize bindless root signature: %s", static_cast<char*>(error_blob->GetBufferPointer()));
                error_blob->Release();
            }
            if (signature_blob) signature_blob->Release();
            return;
        }

        void* layout = nullptr;
        hr = RHI_Context::device->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(),
            IID_PPV_ARGS(reinterpret_cast<ID3D12RootSignature**>(&layout)));
        if (FAILED(hr))
        {
            SP_LOG_ERROR("Failed to create bindless root signature: %s", d3d12_utility::error::dxgi_error_to_string(hr));
        }
        pipeline->SetRhiResourceLayout(layout);

        if (signature_blob) signature_blob->Release();
        if (error_blob) error_blob->Release();
    }

    RHI_Pipeline::~RHI_Pipeline()
    {
        if (m_rhi_resource)
        {
            static_cast<ID3D12PipelineState*>(m_rhi_resource)->Release();
            m_rhi_resource = nullptr;
        }

        if (m_rhi_resource_layout)
        {
            static_cast<ID3D12RootSignature*>(m_rhi_resource_layout)->Release();
            m_rhi_resource_layout = nullptr;
        }
    }
}
