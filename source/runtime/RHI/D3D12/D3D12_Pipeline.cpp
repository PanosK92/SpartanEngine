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
#include "../Rendering/Renderer.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    // forward declarations
    static void create_root_signature(RHI_Pipeline* pipeline, RHI_PipelineState& state, bool is_compute);
    static void create_compute_pipeline(RHI_Pipeline* pipeline, RHI_PipelineState& state);
    static void create_graphics_pipeline(RHI_Pipeline* pipeline, RHI_PipelineState& state);

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
        // create root signature for compute
        create_root_signature(pipeline, state, true);

        if (!pipeline->GetRhiResourceLayout())
        {
            SP_LOG_ERROR("Failed to create root signature for compute pipeline");
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
            SP_LOG_ERROR("Failed to create compute pipeline state: %s", d3d12_utility::error::dxgi_error_to_string(hr));
        }
        pipeline->SetRhiResource(resource);
    }

    static void create_graphics_pipeline(RHI_Pipeline* pipeline, RHI_PipelineState& state)
    {
        // create root signature for graphics
        create_root_signature(pipeline, state, false);

        if (!pipeline->GetRhiResourceLayout())
        {
            SP_LOG_ERROR("Failed to create root signature for graphics pipeline");
            return;
        }

        // rasterizer description
        D3D12_RASTERIZER_DESC desc_rasterizer = {};
        if (state.rasterizer_state)
        {
            desc_rasterizer.FillMode              = d3d12_polygon_mode[static_cast<uint32_t>(state.rasterizer_state->GetPolygonMode())];
            desc_rasterizer.CullMode              = D3D12_CULL_MODE_NONE; // imgui needs no culling
            desc_rasterizer.FrontCounterClockwise = false;
            desc_rasterizer.DepthBias             = static_cast<INT>(state.rasterizer_state->GetDepthBias());
            desc_rasterizer.DepthBiasClamp        = state.rasterizer_state->GetDepthBiasClamp();
            desc_rasterizer.SlopeScaledDepthBias  = state.rasterizer_state->GetDepthBiasSlopeScaled();
            desc_rasterizer.DepthClipEnable       = state.rasterizer_state->GetDepthClipEnabled();
            desc_rasterizer.MultisampleEnable     = false;
            desc_rasterizer.AntialiasedLineEnable = false;
            desc_rasterizer.ForcedSampleCount     = 0;
            desc_rasterizer.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        }
        else
        {
            desc_rasterizer.FillMode        = D3D12_FILL_MODE_SOLID;
            desc_rasterizer.CullMode        = D3D12_CULL_MODE_NONE;
            desc_rasterizer.DepthClipEnable = true;
        }

        // blend state description
        D3D12_BLEND_DESC desc_blend_state = {};
        if (state.blend_state)
        {
            desc_blend_state.AlphaToCoverageEnable                 = false;
            desc_blend_state.IndependentBlendEnable                = false;
            desc_blend_state.RenderTarget[0].BlendEnable           = state.blend_state->GetBlendEnabled();
            desc_blend_state.RenderTarget[0].SrcBlend              = d3d12_blend_factor[static_cast<uint32_t>(state.blend_state->GetSourceBlend())];
            desc_blend_state.RenderTarget[0].DestBlend             = d3d12_blend_factor[static_cast<uint32_t>(state.blend_state->GetDestBlend())];
            desc_blend_state.RenderTarget[0].BlendOp               = d3d12_blend_operation[static_cast<uint32_t>(state.blend_state->GetBlendOp())];
            desc_blend_state.RenderTarget[0].SrcBlendAlpha         = d3d12_blend_factor[static_cast<uint32_t>(state.blend_state->GetSourceBlendAlpha())];
            desc_blend_state.RenderTarget[0].DestBlendAlpha        = d3d12_blend_factor[static_cast<uint32_t>(state.blend_state->GetDestBlendAlpha())];
            desc_blend_state.RenderTarget[0].BlendOpAlpha          = d3d12_blend_operation[static_cast<uint32_t>(state.blend_state->GetBlendOpAlpha())];
            desc_blend_state.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        else
        {
            desc_blend_state.RenderTarget[0].BlendEnable           = false;
            desc_blend_state.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
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
                        attribute.name.c_str(),                              // SemanticName
                        0,                                                   // SemanticIndex
                        d3d12_format[rhi_format_to_index(attribute.format)], // Format
                        0,                                                   // InputSlot
                        attribute.offset,                                    // AlignedByteOffset
                        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,          // InputSlotClass
                        0                                                    // InstanceDataStepRate
                    });
                }
            }

            desc_input_layout.pInputElementDescs = vertex_attributes.data();
            desc_input_layout.NumElements        = static_cast<uint32_t>(vertex_attributes.size());
        }

        // determine render target format
        DXGI_FORMAT rtv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
        if (state.render_target_swapchain)
        {
            rtv_format = d3d12_format[rhi_format_to_index(state.render_target_swapchain->GetFormat())];
        }

        // pipeline description
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout          = desc_input_layout;
        desc.pRootSignature       = static_cast<ID3D12RootSignature*>(pipeline->GetRhiResourceLayout());
        desc.RasterizerState      = desc_rasterizer;
        desc.BlendState           = desc_blend_state;
        desc.DepthStencilState    = desc_depth_stencil_state;
        desc.SampleMask           = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets     = 1;
        desc.RTVFormats[0]        = rtv_format;
        desc.SampleDesc.Count     = 1;

        // vertex shader
        if (state.shaders[RHI_Shader_Type::Vertex])
        {
            desc.VS.pShaderBytecode = state.shaders[RHI_Shader_Type::Vertex]->GetRhiResource();
            desc.VS.BytecodeLength  = state.shaders[RHI_Shader_Type::Vertex]->GetObjectSize();
        }

        // pixel shader
        if (state.shaders[RHI_Shader_Type::Pixel])
        {
            desc.PS.pShaderBytecode = state.shaders[RHI_Shader_Type::Pixel]->GetRhiResource();
            desc.PS.BytecodeLength  = state.shaders[RHI_Shader_Type::Pixel]->GetObjectSize();
        }

        void* resource = nullptr;
        HRESULT hr = RHI_Context::device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(reinterpret_cast<ID3D12PipelineState**>(&resource)));
        if (FAILED(hr))
        {
            SP_LOG_ERROR("Failed to create graphics pipeline state: %s", d3d12_utility::error::dxgi_error_to_string(hr));
        }
        pipeline->SetRhiResource(resource);
    }

    static void create_root_signature(RHI_Pipeline* pipeline, RHI_PipelineState& state, bool is_compute)
    {
        // root parameters:
        // 0 - push constants (32 root constants = 128 bytes for imgui transform matrix)
        // 1 - descriptor table for srv (textures)

        D3D12_ROOT_PARAMETER root_params[2] = {};

        // root constants for push constants (128 bytes = 32 x 32-bit values)
        root_params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_params[0].Constants.ShaderRegister  = 0;
        root_params[0].Constants.RegisterSpace   = 0;
        root_params[0].Constants.Num32BitValues  = 32; // 128 bytes for imgui push constants
        root_params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        // descriptor table for texture srv
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

        // static sampler for texture
        D3D12_STATIC_SAMPLER_DESC sampler_desc = {};
        sampler_desc.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_desc.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler_desc.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler_desc.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler_desc.MipLODBias       = 0.0f;
        sampler_desc.MaxAnisotropy    = 1;
        sampler_desc.ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler_desc.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler_desc.MinLOD           = 0.0f;
        sampler_desc.MaxLOD           = D3D12_FLOAT32_MAX;
        sampler_desc.ShaderRegister   = 0;
        sampler_desc.RegisterSpace    = 0;
        sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // root signature description
        D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {};
        root_sig_desc.NumParameters     = 2;
        root_sig_desc.pParameters       = root_params;
        root_sig_desc.NumStaticSamplers = 1;
        root_sig_desc.pStaticSamplers   = &sampler_desc;
        root_sig_desc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                          D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        // serialize root signature
        ID3DBlob* signature_blob = nullptr;
        ID3DBlob* error_blob     = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error_blob);

        if (FAILED(hr))
        {
            if (error_blob)
            {
                SP_LOG_ERROR("Failed to serialize root signature: %s", static_cast<char*>(error_blob->GetBufferPointer()));
                error_blob->Release();
            }
            if (signature_blob) signature_blob->Release();
            return;
        }

        // create root signature
        void* layout = nullptr;
        hr = RHI_Context::device->CreateRootSignature(
            0,
            signature_blob->GetBufferPointer(),
            signature_blob->GetBufferSize(),
            IID_PPV_ARGS(reinterpret_cast<ID3D12RootSignature**>(&layout))
        );

        if (FAILED(hr))
        {
            SP_LOG_ERROR("Failed to create root signature: %s", d3d12_utility::error::dxgi_error_to_string(hr));
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
