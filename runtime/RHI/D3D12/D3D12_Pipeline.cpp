/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "../Rendering/Renderer.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_Pipeline::RHI_Pipeline(RHI_PipelineState& pipeline_state, RHI_DescriptorSetLayout* descriptor_set_layout)
    {
        m_state = pipeline_state;

        if (pipeline_state.IsCompute())
        {
            // Pipeline description
            D3D12_COMPUTE_PIPELINE_STATE_DESC desc  = {};
            //desc.pRootSignature                     = 0;
            desc.CS                                 = { m_state.shaders[RHI_Shader_Type::Compute]->GetRhiResource(), m_state.shaders[RHI_Shader_Type::Compute]->GetObjectSize()};
            //desc.NodeMask                           = 0;
            //desc.CachedPSO                          = 0;
            //desc.Flags                              = 0;

            d3d12_utility::error::check(RHI_Context::device->CreateComputePipelineState(&desc, IID_PPV_ARGS(reinterpret_cast<ID3D12PipelineState**>(&m_resource_pipeline))));
        }
        else if (pipeline_state.IsGraphics())
        {
            // Rasterizer description
            D3D12_RASTERIZER_DESC desc_rasterizer = {};
            desc_rasterizer.FillMode              = d3d12_polygon_mode[static_cast<uint32_t>(m_state.rasterizer_state->GetPolygonMode())];
            desc_rasterizer.CullMode              = d3d12_cull_mode[static_cast<uint32_t>(RHI_CullMode::Back)];
            desc_rasterizer.FrontCounterClockwise = false;
            desc_rasterizer.DepthBias             = static_cast<INT>(m_state.rasterizer_state->GetDepthBias());
            desc_rasterizer.DepthBiasClamp        = m_state.rasterizer_state->GetDepthBiasClamp();
            desc_rasterizer.SlopeScaledDepthBias  = m_state.rasterizer_state->GetDepthBiasSlopeScaled();
            desc_rasterizer.DepthClipEnable       = m_state.rasterizer_state->GetDepthClipEnabled();
            desc_rasterizer.MultisampleEnable     = false;
            desc_rasterizer.AntialiasedLineEnable = m_state.rasterizer_state->GetLineWidth() > 1.0f;
            desc_rasterizer.ForcedSampleCount     = 0;
            desc_rasterizer.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

            // Blend state description
            D3D12_BLEND_DESC desc_blend_state                      = {};
            desc_blend_state.AlphaToCoverageEnable                 = false;
            desc_blend_state.IndependentBlendEnable                = false;
            desc_blend_state.RenderTarget[0].BlendEnable           = m_state.blend_state->GetBlendEnabled();
            desc_blend_state.RenderTarget[0].SrcBlend              = d3d12_blend_factor[static_cast<uint32_t>(m_state.blend_state->GetSourceBlend())];
            desc_blend_state.RenderTarget[0].DestBlend             = d3d12_blend_factor[static_cast<uint32_t>(m_state.blend_state->GetDestBlend())];
            desc_blend_state.RenderTarget[0].BlendOp               = d3d12_blend_operation[static_cast<uint32_t>(m_state.blend_state->GetBlendOp())];
            desc_blend_state.RenderTarget[0].SrcBlendAlpha         = d3d12_blend_factor[static_cast<uint32_t>(m_state.blend_state->GetSourceBlendAlpha())];
            desc_blend_state.RenderTarget[0].DestBlendAlpha        = d3d12_blend_factor[static_cast<uint32_t>(m_state.blend_state->GetDestBlendAlpha())];
            desc_blend_state.RenderTarget[0].BlendOpAlpha          = d3d12_blend_operation[static_cast<uint32_t>(m_state.blend_state->GetBlendOpAlpha())];
            desc_blend_state.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

            // Depth-stencil state
            D3D12_DEPTH_STENCIL_DESC desc_depth_stencil_state     = {};
            desc_depth_stencil_state.DepthEnable                  = m_state.depth_stencil_state->GetDepthTestEnabled();
            desc_depth_stencil_state.DepthWriteMask               = m_state.depth_stencil_state->GetDepthWriteEnabled() ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            desc_depth_stencil_state.DepthFunc                    = d3d12_comparison_function[static_cast<uint32_t>(m_state.depth_stencil_state->GetDepthComparisonFunction())];
            desc_depth_stencil_state.StencilEnable                = static_cast<BOOL>(m_state.depth_stencil_state->GetStencilTestEnabled() || m_state.depth_stencil_state->GetStencilWriteEnabled());
            desc_depth_stencil_state.StencilReadMask              = m_state.depth_stencil_state->GetStencilReadMask();
            desc_depth_stencil_state.StencilWriteMask             = m_state.depth_stencil_state->GetStencilWriteMask();
            desc_depth_stencil_state.FrontFace.StencilFailOp      = d3d12_stencil_operation[static_cast<uint32_t>(m_state.depth_stencil_state->GetStencilFailOperation())];
            desc_depth_stencil_state.FrontFace.StencilDepthFailOp = d3d12_stencil_operation[static_cast<uint32_t>(m_state.depth_stencil_state->GetStencilDepthFailOperation())];
            desc_depth_stencil_state.FrontFace.StencilPassOp      = d3d12_stencil_operation[static_cast<uint32_t>(m_state.depth_stencil_state->GetStencilPassOperation())];
            desc_depth_stencil_state.FrontFace.StencilFunc        = d3d12_comparison_function[static_cast<uint32_t>(m_state.depth_stencil_state->GetStencilComparisonFunction())];
            desc_depth_stencil_state.BackFace                     = desc_depth_stencil_state.FrontFace;

            // Input layout
            D3D12_INPUT_LAYOUT_DESC desc_input_layout = {};
            vector<D3D12_INPUT_ELEMENT_DESC> vertex_attributes;
            RHI_Shader* shader_vertex = m_state.shaders[RHI_Shader_Type::Vertex];
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

            // Pipeline description
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
            desc.InputLayout                        = desc_input_layout;
            //desc.pRootSignature                     = m_rootSignature.Get();
            desc.VS                                 = { m_state.shaders[RHI_Shader_Type::Vertex]->GetRhiResource(), m_state.shaders[RHI_Shader_Type::Vertex]->GetObjectSize()};
            desc.PS                                 = { m_state.shaders[RHI_Shader_Type::Pixel]->GetRhiResource(),  m_state.shaders[RHI_Shader_Type::Pixel]->GetObjectSize() };
            desc.RasterizerState                    = desc_rasterizer;
            desc.BlendState                         = desc_blend_state;
            desc.DepthStencilState                  = desc_depth_stencil_state;
            desc.SampleMask                         = UINT_MAX;
            desc.PrimitiveTopologyType              = d3d12_primitive_topology[static_cast<uint32_t>(RHI_PrimitiveTopology::TriangleList)];
            desc.NumRenderTargets                   = 1;
            desc.RTVFormats[0]                      = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count                   = 1;

            d3d12_utility::error::check(RHI_Context::device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(reinterpret_cast<ID3D12PipelineState**>(&m_resource_pipeline))));
        }
    }
    
    RHI_Pipeline::~RHI_Pipeline() = default;
}
