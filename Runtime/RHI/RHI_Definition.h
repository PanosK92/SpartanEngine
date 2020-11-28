/*
Copyright(c) 2016-2020 Panos Karabelas

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

#pragma once

//= INCLUDES ===============
#include <stdint.h>
#include "../Math/Vector4.h"
//==========================

// Declarations
namespace Spartan
{
    struct RHI_Context;
    class RHI_Device;
    class RHI_CommandList;
    class RHI_PipelineState;
    class RHI_PipelineCache;
    class RHI_Pipeline;
    class RHI_DescriptorSetLayout;
    class RHI_DescriptorCache;
    class RHI_SwapChain;
    class RHI_RasterizerState;
    class RHI_BlendState;
    class RHI_DepthStencilState;
    class RHI_InputLayout;
    class RHI_VertexBuffer;
    class RHI_IndexBuffer;
    class RHI_ConstantBuffer;
    class RHI_Sampler;
    class RHI_Viewport;
    class RHI_Texture;
    class RHI_Texture2D;
    class RHI_TextureCube;
    class RHI_Shader;
    class RHI_Semaphore;
    class RHI_Fence;
    struct RHI_Vertex_Undefined;
    struct RHI_Vertex_PosTex;
    struct RHI_Vertex_PosCol;
    struct RHI_Vertex_PosUvCol;
    struct RHI_Vertex_PosTexNorTan;

    enum RHI_PhysicalDevice_Type
    {
        RHI_PhysicalDevice_Unknown,
        RHI_PhysicalDevice_Integrated,
        RHI_PhysicalDevice_Discrete,
        RHI_PhysicalDevice_Virtual,
        RHI_PhysicalDevice_Cpu
    };

    enum RHI_Api_Type
    {
        RHI_Api_D3d11,
        RHI_Api_D3d12,
        RHI_Api_Vulkan
    };

    enum RHI_Present_Mode : uint32_t
    {
        RHI_Present_Immediate                   = 1 << 0, // Doesn't wait.                  Frames are not dropped. Tearing.
        RHI_Present_Mailbox                     = 1 << 1, // Waits for v-blank.             Frames are dropped.     No tearing.
        RHI_Present_Fifo                        = 1 << 2, // Waits for v-blank, every time. Frames are not dropped. No tearing.
        RHI_Present_FifoRelaxed                 = 1 << 3, // Waits for v-blank, once.       Frames are not dropped. Tearing.
        RHI_Present_SharedDemandRefresh         = 1 << 4,
        RHI_Present_SharedDContinuousRefresh    = 1 << 5,

        // D3D11 only flags as match to Vulkan is possible
        RHI_Swap_Discard                = 1 << 6,
        RHI_Swap_Sequential             = 1 << 7,
        RHI_Swap_Flip_Sequential        = 1 << 8,
        RHI_Swap_Flip_Discard           = 1 << 9,
        RHI_SwapChain_Allow_Mode_Switch = 1 << 10
    };

    enum RHI_Queue_Type
    {
        RHI_Queue_Graphics,
        RHI_Queue_Transfer,
        RHI_Queue_Compute,
        RHI_Queue_Undefined
    };

    enum RHI_Query_Type
    {
        RHI_Query_Timestamp,
        RHI_Query_Timestamp_Disjoint
    };

    enum RHI_PrimitiveTopology_Mode
    {
        RHI_PrimitiveTopology_TriangleList,
        RHI_PrimitiveTopology_LineList,
        RHI_PrimitiveTopology_Unknown
    };

    enum RHI_Cull_Mode
    {
        RHI_Cull_None,
        RHI_Cull_Front,
        RHI_Cull_Back,
        RHI_Cull_Undefined
    };

    enum RHI_Fill_Mode
    {
        RHI_Fill_Solid,
        RHI_Fill_Wireframe,
        RHI_Fill_Undefined
    };

    enum RHI_Filter
    {
        RHI_Filter_Nearest,
        RHI_Filter_Linear,
    };

    enum RHI_Sampler_Mipmap_Mode
    {
        RHI_Sampler_Mipmap_Nearest,
        RHI_Sampler_Mipmap_Linear,
    };

    enum RHI_Sampler_Address_Mode
    {
        RHI_Sampler_Address_Wrap,
        RHI_Sampler_Address_Mirror,
        RHI_Sampler_Address_Clamp,
        RHI_Sampler_Address_Border,
        RHI_Sampler_Address_MirrorOnce,
    };

    enum RHI_Comparison_Function
    {
        RHI_Comparison_Never,
        RHI_Comparison_Less,
        RHI_Comparison_Equal,
        RHI_Comparison_LessEqual,
        RHI_Comparison_Greater,
        RHI_Comparison_NotEqual,
        RHI_Comparison_GreaterEqual,
        RHI_Comparison_Always
    };

    enum RHI_Stencil_Operation
    {
        RHI_Stencil_Keep,
        RHI_Stencil_Zero,
        RHI_Stencil_Replace,
        RHI_Stencil_Incr_Sat,
        RHI_Stencil_Decr_Sat,
        RHI_Stencil_Invert,
        RHI_Stencil_Incr,
        RHI_Stencil_Decr
    };

    enum RHI_Format : uint32_t // gets serialized so better be explicit
    {
        // R
        RHI_Format_R8_Unorm,
        RHI_Format_R16_Uint,
        RHI_Format_R16_Float,
        RHI_Format_R32_Uint,
        RHI_Format_R32_Float,
        // RG
        RHI_Format_R8G8_Unorm,
        RHI_Format_R16G16_Float,
        RHI_Format_R32G32_Float,
        // RGB
        RHI_Format_R11G11B10_Float,
        RHI_Format_R16G16B16A16_Snorm,
        RHI_Format_R32G32B32_Float,
        // RGBA
        RHI_Format_R8G8B8A8_Unorm,
        RHI_Format_R10G10B10A2_Unorm,
        RHI_Format_R16G16B16A16_Float,
        RHI_Format_R32G32B32A32_Float,
        // DEPTH
        RHI_Format_D32_Float,
        RHI_Format_D32_Float_S8X24_Uint,

        RHI_Format_Undefined
    };

    enum RHI_Blend
    {
        RHI_Blend_Zero,
        RHI_Blend_One,
        RHI_Blend_Src_Color,
        RHI_Blend_Inv_Src_Color,
        RHI_Blend_Src_Alpha,
        RHI_Blend_Inv_Src_Alpha,
        RHI_Blend_Dest_Alpha,
        RHI_Blend_Inv_Dest_Alpha,
        RHI_Blend_Dest_Color,
        RHI_Blend_Inv_Dest_Color,
        RHI_Blend_Src_Alpha_Sat,
        RHI_Blend_Blend_Factor,
        RHI_Blend_Inv_Blend_Factor,
        RHI_Blend_Src1_Color,
        RHI_Blend_Inv_Src1_Color,
        RHI_Blend_Src1_Alpha,
        RHI_Blend_Inv_Src1_Alpha
    };

    enum RHI_Blend_Operation
    {
        RHI_Blend_Operation_Add,
        RHI_Blend_Operation_Subtract,
        RHI_Blend_Operation_Rev_Subtract,
        RHI_Blend_Operation_Min,
        RHI_Blend_Operation_Max
    };

    enum RHI_Descriptor_Type
    {
        RHI_Descriptor_Sampler,
        RHI_Descriptor_Texture,
        RHI_Descriptor_ConstantBuffer,
        RHI_Descriptor_Undefined
    };

    enum class RHI_Image_Layout
    {
        Undefined,
        General,
        Preinitialized,
        Color_Attachment_Optimal,
        Depth_Stencil_Attachment_Optimal,
        Depth_Stencil_Read_Only_Optimal,
        Shader_Read_Only_Optimal,
        Transfer_Dst_Optimal,
        Present_Src
    };

    enum class RHI_Semaphore_State
    {
        Idle,
        Submitted,
        Signaled
    };

    inline const char* rhi_format_to_string(const RHI_Format result)
    {
        switch (result)
        {
            case RHI_Format_R8_Unorm:               return "RHI_Format_R8_Unorm";
            case RHI_Format_R16_Uint:               return "RHI_Format_R16_Uint";
            case RHI_Format_R16_Float:              return "RHI_Format_R16_Float";
            case RHI_Format_R32_Uint:               return "RHI_Format_R32_Uint";
            case RHI_Format_R32_Float:              return "RHI_Format_R32_Float";
            case RHI_Format_R8G8_Unorm:             return "RHI_Format_R8G8_Unorm";
            case RHI_Format_R16G16_Float:           return "RHI_Format_R16G16_Float";
            case RHI_Format_R32G32_Float:           return "RHI_Format_R32G32_Float";
            case RHI_Format_R32G32B32_Float:        return "RHI_Format_R32G32B32_Float";
            case RHI_Format_R8G8B8A8_Unorm:         return "RHI_Format_R8G8B8A8_Unorm";
            case RHI_Format_R16G16B16A16_Float:     return "RHI_Format_R16G16B16A16_Float";
            case RHI_Format_R32G32B32A32_Float:     return "RHI_Format_R32G32B32A32_Float";
            case RHI_Format_D32_Float:              return "RHI_Format_D32_Float";
            case RHI_Format_D32_Float_S8X24_Uint:   return "RHI_Format_D32_Float_S8X24_Uint";
            case RHI_Format_Undefined:              return "RHI_Format_Undefined";
        }

        return "Unknown format";
    }

    enum RHI_Shader_Type : uint8_t
    {
        RHI_Shader_Unknown  = 0,
        RHI_Shader_Vertex   = 1 << 0,
        RHI_Shader_Pixel    = 1 << 1,
        RHI_Shader_Compute  = 1 << 2,
    };

    enum Shader_Compilation_State
    {
        Shader_Compilation_Unknown,
        Shader_Compilation_Compiling,
        Shader_Compilation_Succeeded,
        Shader_Compilation_Failed
    };

    // Shader resource slot shifts (required to produce spirv from hlsl)
    static const uint32_t rhi_shader_shift_storage_texture  = 000;
    static const uint32_t rhi_shader_shift_buffer           = 100;
    static const uint32_t rhi_shader_shift_texture          = 200;
    static const uint32_t rhi_shader_shift_sampler          = 300;
    
    // Descriptor set limits
    static const uint8_t rhi_descriptor_max_storage_textures            = 10;
    static const uint8_t rhi_descriptor_max_constant_buffers            = 10;
    static const uint8_t rhi_descriptor_max_constant_buffers_dynamic    = 10;
    static const uint8_t rhi_descriptor_max_samplers                    = 10;
    static const uint8_t rhi_descriptor_max_textures                    = 10;
    
    static const Math::Vector4  rhi_color_dont_care           = Math::Vector4(-std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f);
    static const Math::Vector4  rhi_color_load                = Math::Vector4(std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f);
    static const float          rhi_depth_dont_care           = -std::numeric_limits<float>::infinity();
    static const float          rhi_depth_load                = std::numeric_limits<float>::infinity();
    static const uint32_t       rhi_stencil_dont_care         = (std::numeric_limits<uint32_t>::max)();
    static const uint32_t       rhi_stencil_load              = (std::numeric_limits<uint32_t>::max)() - 1;
    static const uint8_t        rhi_max_render_target_count   = 8;
    static const uint8_t        rhi_max_constant_buffer_count = 8;
    static const uint32_t       rhi_dynamic_offset_empty      = (std::numeric_limits<uint32_t>::max)();
}
