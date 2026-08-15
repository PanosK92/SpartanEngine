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

//= INCLUDES =========================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Texture.h"
#include "../RHI_DirectXShaderCompiler.h"
#include "D3D12_Internal.h"
#include "D3D12_BlitGraphics.h"
#include <unordered_map>
#include <mutex>
//====================================

using namespace std;

namespace spartan::d3d12_blit
{
    // small fullscreen triangle vs + ps_color/ps_depth, kept as inline source so the d3d12 backend stays self contained
    // dxc compiles them at first use, the resulting bytecode is cached and reused for the lifetime of the process
    static const char* shader_source = R"(
struct VsOut
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

cbuffer Params : register(b0)
{
    float source_uv_scale_x;
    float source_uv_scale_y;
    float pad0;
    float pad1;
};

VsOut main_vs(uint id : SV_VertexID)
{
    VsOut o;
    float2 ndc = float2((id << 1) & 2, id & 2);
    o.position = float4(ndc * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv       = ndc * float2(source_uv_scale_x, source_uv_scale_y);
    return o;
}

Texture2D<float4> src_tex  : register(t0);
SamplerState      s_linear : register(s0);
SamplerState      s_point  : register(s1);

float4 main_ps_color(VsOut v) : SV_Target
{
    return src_tex.SampleLevel(s_linear, v.uv, 0);
}

float main_ps_depth(VsOut v) : SV_Depth
{
    return src_tex.SampleLevel(s_point, v.uv, 0).r;
}
)";

    // root sig layout, kept tight for the blit alone, decoupled from the bindless one used by the rest of the renderer
    // param 0: 32 bit constants b0 (source uv scale)
    // param 1: srv table t0 (source texture)
    // static samplers s0 (linear) and s1 (point)
    static const uint32_t root_param_constants = 0;
    static const uint32_t root_param_srv_table = 1;

    static mutex                                                init_mutex;
    static bool                                                 initialized      = false;
    static ID3D12RootSignature*                                 root_signature   = nullptr;
    static IDxcBlob*                                            vs_bytecode      = nullptr;
    static IDxcBlob*                                            ps_color_bytecode = nullptr;
    static IDxcBlob*                                            ps_depth_bytecode = nullptr;

    struct PsoKey
    {
        DXGI_FORMAT format;
        bool        is_depth;
        bool operator==(const PsoKey& o) const { return format == o.format && is_depth == o.is_depth; }
    };
    struct PsoKeyHash { size_t operator()(const PsoKey& k) const { return (size_t)k.format ^ ((size_t)k.is_depth << 16); } };

    static unordered_map<PsoKey, ID3D12PipelineState*, PsoKeyHash> pso_cache;
    static mutex                                                   pso_cache_mutex;

    // helper, returns the depth-stencil format that maps a typeless or float depth source to the proper dsv format
    static DXGI_FORMAT to_dsv_format(DXGI_FORMAT f)
    {
        switch (f)
        {
            case DXGI_FORMAT_D16_UNORM:         return DXGI_FORMAT_D16_UNORM;
            case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
            case DXGI_FORMAT_D32_FLOAT:         return DXGI_FORMAT_D32_FLOAT;
            case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
            case DXGI_FORMAT_R32_TYPELESS:      return DXGI_FORMAT_D32_FLOAT;
            case DXGI_FORMAT_R32_FLOAT:         return DXGI_FORMAT_D32_FLOAT;
            case DXGI_FORMAT_R16_TYPELESS:      return DXGI_FORMAT_D16_UNORM;
            case DXGI_FORMAT_R24G8_TYPELESS:    return DXGI_FORMAT_D24_UNORM_S8_UINT;
            default:                            return f;
        }
    }

    static IDxcBlob* compile_blob(const char* entry_point, const char* target_profile)
    {
        vector<string> args;
        args.emplace_back("-E"); args.emplace_back(entry_point);
        args.emplace_back("-T"); args.emplace_back(target_profile);
        args.emplace_back("-HV"); args.emplace_back("2021");
        args.emplace_back("-Wno-ignored-attributes");
        args.emplace_back("-Qstrip_debug");
        args.emplace_back("-Qstrip_reflect");

        string source = shader_source;
        IDxcResult* result = DirectXShaderCompiler::Compile(source, args);
        if (!result)
        {
            SP_LOG_ERROR("Failed to compile blit shader entry '%s'", entry_point);
            return nullptr;
        }

        IDxcBlob* bytecode = nullptr;
        result->GetResult(&bytecode);
        result->Release();
        return bytecode;
    }

    static void initialize_internal()
    {
        if (initialized)
        {
            return;
        }

        // compile shaders
        vs_bytecode       = compile_blob("main_vs",       "vs_6_6");
        ps_color_bytecode = compile_blob("main_ps_color", "ps_6_6");
        ps_depth_bytecode = compile_blob("main_ps_depth", "ps_6_6");

        // build root signature
        D3D12_DESCRIPTOR_RANGE srv_range = {};
        srv_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors                    = 1;
        srv_range.BaseShaderRegister                = 0;
        srv_range.RegisterSpace                     = 0;
        srv_range.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER params[2] = {};
        params[root_param_constants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[root_param_constants].Constants.ShaderRegister = 0;
        params[root_param_constants].Constants.RegisterSpace  = 0;
        params[root_param_constants].Constants.Num32BitValues = 4;
        params[root_param_constants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

        params[root_param_srv_table].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[root_param_srv_table].DescriptorTable.NumDescriptorRanges = 1;
        params[root_param_srv_table].DescriptorTable.pDescriptorRanges   = &srv_range;
        params[root_param_srv_table].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
        samplers[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
        samplers[0].MaxLOD           = D3D12_FLOAT32_MAX;
        samplers[0].ShaderRegister   = 0;
        samplers[0].RegisterSpace    = 0;
        samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        samplers[1].Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
        samplers[1].MaxLOD           = D3D12_FLOAT32_MAX;
        samplers[1].ShaderRegister   = 1;
        samplers[1].RegisterSpace    = 0;
        samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {};
        root_sig_desc.NumParameters     = 2;
        root_sig_desc.pParameters       = params;
        root_sig_desc.NumStaticSamplers = 2;
        root_sig_desc.pStaticSamplers   = samplers;
        root_sig_desc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
                                        | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
                                        | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
                                        | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        ID3DBlob* signature_blob = nullptr;
        ID3DBlob* error_blob     = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error_blob);
        if (FAILED(hr))
        {
            if (error_blob)
            {
                SP_LOG_ERROR("Failed to serialize blit root signature: %s", static_cast<char*>(error_blob->GetBufferPointer()));
                error_blob->Release();
            }
            if (signature_blob)
            {
                signature_blob->Release();
            }
            return;
        }

        RHI_Context::device->CreateRootSignature(
            0,
            signature_blob->GetBufferPointer(),
            signature_blob->GetBufferSize(),
            IID_PPV_ARGS(&root_signature)
        );
        if (signature_blob)
        {
            signature_blob->Release();
        }
        if (error_blob)
        {
            error_blob->Release();
        }

        initialized = (root_signature != nullptr) && (vs_bytecode != nullptr) && (ps_color_bytecode != nullptr) && (ps_depth_bytecode != nullptr);
    }

    void initialize()
    {
        lock_guard<mutex> lock(init_mutex);
        initialize_internal();
    }

    static ID3D12PipelineState* get_or_create_pso(DXGI_FORMAT target_format, bool is_depth)
    {
        PsoKey key{ target_format, is_depth };

        {
            lock_guard<mutex> lock(pso_cache_mutex);
            auto it = pso_cache.find(key);
            if (it != pso_cache.end())
            {
                return it->second;
            }
        }

        if (!initialized)
        {
            initialize();
        }
        if (!initialized)
        {
            return nullptr;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature                          = root_signature;
        desc.VS.pShaderBytecode                      = vs_bytecode->GetBufferPointer();
        desc.VS.BytecodeLength                       = vs_bytecode->GetBufferSize();
        IDxcBlob* ps_blob                            = is_depth ? ps_depth_bytecode : ps_color_bytecode;
        desc.PS.pShaderBytecode                      = ps_blob->GetBufferPointer();
        desc.PS.BytecodeLength                       = ps_blob->GetBufferSize();
        desc.SampleMask                              = UINT_MAX;
        desc.PrimitiveTopologyType                   = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.SampleDesc.Count                        = 1;

        // rasterizer, no culling so a back-facing fullscreen triangle still renders regardless of vertex order
        desc.RasterizerState.FillMode                = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode                = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.FrontCounterClockwise   = FALSE;
        desc.RasterizerState.DepthClipEnable         = TRUE;

        // blend state, single rt with full write mask, blending disabled
        desc.BlendState.RenderTarget[0].BlendEnable    = FALSE;
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        if (is_depth)
        {
            desc.NumRenderTargets                   = 0;
            desc.DSVFormat                          = to_dsv_format(target_format);
            desc.DepthStencilState.DepthEnable      = TRUE;
            desc.DepthStencilState.DepthWriteMask   = D3D12_DEPTH_WRITE_MASK_ALL;
            desc.DepthStencilState.DepthFunc        = D3D12_COMPARISON_FUNC_ALWAYS;
            desc.DepthStencilState.StencilEnable    = FALSE;
        }
        else
        {
            desc.NumRenderTargets                   = 1;
            desc.RTVFormats[0]                      = target_format;
            desc.DepthStencilState.DepthEnable      = FALSE;
            desc.DepthStencilState.StencilEnable    = FALSE;
        }

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = RHI_Context::device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
        if (FAILED(hr))
        {
            SP_LOG_ERROR("Failed to create blit pipeline state for format %u (is_depth=%d)", target_format, is_depth ? 1 : 0);
            return nullptr;
        }

        {
            lock_guard<mutex> lock(pso_cache_mutex);
            // double-check after locking, another thread may have inserted the same key while we created the pso
            auto it = pso_cache.find(key);
            if (it != pso_cache.end())
            {
                pso->Release();
                return it->second;
            }
            pso_cache[key] = pso;
        }

        return pso;
    }

    void blit(
        ID3D12GraphicsCommandList* cmd_list,
        const BlitParams&          params
    )
    {
        if (!initialized)
        {
            initialize();
        }
        if (!initialized)
        {
            return;
        }

        const bool is_depth        = params.is_depth_destination;
        DXGI_FORMAT target_format  = params.destination_format;
        ID3D12PipelineState* pso   = get_or_create_pso(target_format, is_depth);
        if (!pso)
        {
            return;
        }

        // allocate a transient ring slot in the shader-visible cbv/srv/uav heap and copy the source srv into it
        // the source texture's existing srv lives in the cpu-only staging heap, so it can't be referenced directly by the gpu
        uint32_t slot                            = d3d12_descriptors::AllocateRing(1);
        D3D12_CPU_DESCRIPTOR_HANDLE dst_cpu      = d3d12_descriptors::GetCbvSrvUavGpuVisibleCpuHandle(slot);
        D3D12_GPU_DESCRIPTOR_HANDLE dst_gpu      = d3d12_descriptors::GetCbvSrvUavGpuHandle(slot);
        RHI_Context::device->CopyDescriptorsSimple(1, dst_cpu, params.source_srv_cpu_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // bind the blit root sig and pso, the next SetPipelineState in the engine restores the bindless root sig and tables
        cmd_list->SetGraphicsRootSignature(root_signature);
        cmd_list->SetPipelineState(pso);
        cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd_list->IASetVertexBuffers(0, 0, nullptr);
        cmd_list->IASetIndexBuffer(nullptr);

        // upload the uv scale that maps the destination 0..1 onto the requested source sub-extent
        // matches the vulkan blit semantics where the source extent is source_size * source_scaling
        float consts[4] = { params.source_uv_scale_x, params.source_uv_scale_y, 0.0f, 0.0f };
        cmd_list->SetGraphicsRoot32BitConstants(root_param_constants, 4, consts, 0);
        cmd_list->SetGraphicsRootDescriptorTable(root_param_srv_table, dst_gpu);

        // viewport and scissor at full destination extent, the fullscreen triangle covers all of it
        D3D12_VIEWPORT viewport = {};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width    = static_cast<float>(params.destination_width);
        viewport.Height   = static_cast<float>(params.destination_height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        cmd_list->RSSetViewports(1, &viewport);

        D3D12_RECT scissor = {};
        scissor.left   = 0;
        scissor.top    = 0;
        scissor.right  = static_cast<LONG>(params.destination_width);
        scissor.bottom = static_cast<LONG>(params.destination_height);
        cmd_list->RSSetScissorRects(1, &scissor);

        if (is_depth)
        {
            cmd_list->OMSetRenderTargets(0, nullptr, FALSE, &params.destination_dsv_handle);
        }
        else
        {
            cmd_list->OMSetRenderTargets(1, &params.destination_rtv_handle, FALSE, nullptr);
        }

        cmd_list->DrawInstanced(3, 1, 0, 0);
    }
}
