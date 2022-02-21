/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include <cstdint>
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
    class RHI_DescriptorSet;
    class RHI_DescriptorSetLayout;
    class RHI_DescriptorSetLayoutCache;
    class RHI_SwapChain;
    class RHI_RasterizerState;
    class RHI_BlendState;
    class RHI_DepthStencilState;
    class RHI_InputLayout;
    class RHI_VertexBuffer;
    class RHI_IndexBuffer;
    class RHI_StructuredBuffer;
    class RHI_ConstantBuffer;
    class RHI_Sampler;
    class RHI_Viewport;
    class RHI_Texture;
    class RHI_Texture2D;
    class RHI_Texture2DArray;
    class RHI_TextureCube;
    class RHI_Shader;
    class RHI_Semaphore;
    class RHI_Fence;
    struct RHI_Texture_Mip;
    struct RHI_Texture_Slice;
    struct RHI_Vertex_Undefined;
    struct RHI_Vertex_PosTex;
    struct RHI_Vertex_PosCol;
    struct RHI_Vertex_PosUvCol;
    struct RHI_Vertex_PosTexNorTan;

    enum class RHI_PhysicalDevice_Type
    {
        Unknown,
        Integrated,
        Discrete,
        Virtual,
        Cpu
    };

    enum class RHI_Api_Type
    {
        D3d11,
        D3d12,
        Vulkan
    };

    enum RHI_Present_Mode : uint32_t
    {
        RHI_Present_Immediate                = 1 << 0, // Doesn't wait.                  Frames are not dropped. Tearing.
        RHI_Present_Mailbox                  = 1 << 1, // Waits for v-blank.             Frames are dropped.     No tearing.
        RHI_Present_Fifo                     = 1 << 2, // Waits for v-blank, every time. Frames are not dropped. No tearing.
        RHI_Present_FifoRelaxed              = 1 << 3, // Waits for v-blank, once.       Frames are not dropped. Tearing.
        RHI_Present_SharedDemandRefresh      = 1 << 4,
        RHI_Present_SharedDContinuousRefresh = 1 << 5,

        // D3D11 only flags as match to Vulkan is possible
        RHI_Swap_Discard                = 1 << 6,
        RHI_Swap_Sequential             = 1 << 7,
        RHI_Swap_Flip_Sequential        = 1 << 8,
        RHI_Swap_Flip_Discard           = 1 << 9,
        RHI_SwapChain_Allow_Mode_Switch = 1 << 10
    };

    enum class RHI_Queue_Type
    {
        Graphics,
        Compute,
        Copy,
        Undefined
    };

    enum class RHI_Query_Type
    {
        Timestamp,
        Timestamp_Disjoint
    };

    enum class RHI_PrimitiveTopology_Mode
    {
        TriangleList,
        LineList,
        RHI_PrimitiveTopology_Unknown
    };

    enum class RHI_CullMode
    {
        None,
        Front,
        Back,
        Undefined
    };

    enum class RHI_PolygonMode
    {
        Solid,
        Wireframe,
        Undefined
    };

    enum class RHI_Filter
    {
        Nearest,
        Linear,
    };

    enum class RHI_Sampler_Mipmap_Mode
    {
        Nearest,
        Linear,
    };

    enum class RHI_Sampler_Address_Mode
    {
        Wrap,
        Mirror,
        Clamp,
        Border,
        MirrorOnce,
    };

    enum class RHI_Comparison_Function
    {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always
    };

    enum class RHI_Stencil_Operation
    {
        Keep,
        Zero,
        Replace,
        Incr_Sat,
        Decr_Sat,
        Invert,
        Incr,
        Decr
    };

    enum RHI_Format : uint32_t // gets serialized so better be explicit
    {
        // R
        RHI_Format_R8_Unorm,
        RHI_Format_R8_Uint,
        RHI_Format_R16_Unorm,
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
        RHI_Format_R32G32B32_Float,
        // RGBA
        RHI_Format_R8G8B8A8_Unorm,
        RHI_Format_R10G10B10A2_Unorm,
        RHI_Format_R16G16B16A16_Unorm,
        RHI_Format_R16G16B16A16_Snorm,
        RHI_Format_R16G16B16A16_Float,
        RHI_Format_R32G32B32A32_Float,
        // DEPTH
        RHI_Format_D16_Unorm,
        RHI_Format_D32_Float,
        RHI_Format_D32_Float_S8X24_Uint,
        // Compressed
        RHI_Format_BC7,
        RHI_Format_ASTC,

        RHI_Format_Undefined
    };

    enum class RHI_Blend
    {
        Zero,
        One,
        Src_Color,
        Inv_Src_Color,
        Src_Alpha,
        Inv_Src_Alpha,
        Dest_Alpha,
        Inv_Dest_Alpha,
        Dest_Color,
        Inv_Dest_Color,
        Src_Alpha_Sat,
        Blend_Factor,
        Inv_Blend_Factor,
        Src1_Color,
        Inv_Src1_Color,
        Src1_Alpha,
        Inv_Src1_Alpha
    };

    enum class RHI_Blend_Operation
    {
        Add,
        Subtract,
        Rev_Subtract,
        Min,
        Max
    };

    enum class RHI_Descriptor_Type
    {
        Sampler,
        Texture,
        TextureStorage,
        ConstantBuffer,
        StructuredBuffer,
        Undefined
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
        Transfer_Src_Optimal,
        Transfer_Dst_Optimal,
        Present_Src
    };

    enum class RHI_Semaphore_State
    {
        Idle,
        Submitted,
        Signaled
    };

    enum RHI_Shader_Type : uint8_t
    {
        RHI_Shader_Unknown  = 0,
        RHI_Shader_Vertex   = 1 << 0,
        RHI_Shader_Pixel    = 1 << 1,
        RHI_Shader_Compute  = 1 << 2,
    };

    enum class Shader_Compilation_State
    {
        Idle,
        Compiling,
        Succeeded,
        Failed
    };

    // Shader register slot shifts (required to produce spirv from hlsl)
    static const uint32_t rhi_shader_shift_register_u = 000;
    static const uint32_t rhi_shader_shift_register_b = 100;
    static const uint32_t rhi_shader_shift_register_t = 200;
    static const uint32_t rhi_shader_shift_register_s = 300;

    // Descriptor set limits
    static const uint8_t rhi_descriptor_max_storage_textures         = 10;
    static const uint8_t rhi_descriptor_max_storage_buffers          = 10;
    static const uint8_t rhi_descriptor_max_constant_buffers         = 10;
    static const uint8_t rhi_descriptor_max_constant_buffers_dynamic = 10;
    static const uint8_t rhi_descriptor_max_samplers                 = 10;
    static const uint8_t rhi_descriptor_max_textures                 = 10;
    
    static const Math::Vector4  rhi_color_dont_care           = Math::Vector4(-std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f);
    static const Math::Vector4  rhi_color_load                = Math::Vector4(std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f);
    static const float          rhi_depth_dont_care           = -std::numeric_limits<float>::infinity();
    static const float          rhi_depth_load                = std::numeric_limits<float>::infinity();
    static const uint32_t       rhi_stencil_dont_care         = (std::numeric_limits<uint32_t>::max)();
    static const uint32_t       rhi_stencil_load              = (std::numeric_limits<uint32_t>::max)() - 1;
    static const uint8_t        rhi_max_render_target_count   = 8;
    static const uint8_t        rhi_max_constant_buffer_count = 8;
    static const uint32_t       rhi_dynamic_offset_empty      = (std::numeric_limits<uint32_t>::max)();

    constexpr uint32_t RhiFormatToBitsPerChannel(const RHI_Format format)
    {
        uint32_t bits = 0;

        switch (format)
        {
            case RHI_Format_R8_Unorm:           bits = 8;  break;
            case RHI_Format_R8_Uint:            bits = 8;  break;
            case RHI_Format_R16_Unorm:          bits = 16; break;
            case RHI_Format_R16_Uint:           bits = 16; break;
            case RHI_Format_R16_Float:          bits = 16; break;
            case RHI_Format_R32_Uint:           bits = 32; break;
            case RHI_Format_R32_Float:          bits = 32; break;
            case RHI_Format_R8G8_Unorm:         bits = 8;  break;
            case RHI_Format_R16G16_Float:       bits = 16; break;
            case RHI_Format_R32G32_Float:       bits = 32; break;
            case RHI_Format_R32G32B32_Float:    bits = 32; break;
            case RHI_Format_R8G8B8A8_Unorm:     bits = 8;  break;
            case RHI_Format_R16G16B16A16_Unorm: bits = 16; break;
            case RHI_Format_R16G16B16A16_Snorm: bits = 16; break;
            case RHI_Format_R16G16B16A16_Float: bits = 16; break;
            case RHI_Format_R32G32B32A32_Float: bits = 32; break;
        }

        return bits;
    }

    constexpr uint32_t RhiFormatToChannelCount(const RHI_Format format)
    {
        uint32_t channel_count = 0;

        switch (format)
        {
            case RHI_Format_R8_Unorm:           channel_count = 1; break;
            case RHI_Format_R8_Uint:            channel_count = 1; break;
            case RHI_Format_R16_Unorm:          channel_count = 1; break;
            case RHI_Format_R16_Uint:           channel_count = 1; break;
            case RHI_Format_R16_Float:          channel_count = 1; break;
            case RHI_Format_R32_Uint:           channel_count = 1; break;
            case RHI_Format_R32_Float:          channel_count = 1; break;
            case RHI_Format_R8G8_Unorm:         channel_count = 2; break;
            case RHI_Format_R16G16_Float:       channel_count = 2; break;
            case RHI_Format_R32G32_Float:       channel_count = 2; break;
            case RHI_Format_R11G11B10_Float:    channel_count = 3; break;
            case RHI_Format_R32G32B32_Float:    channel_count = 3; break;
            case RHI_Format_R8G8B8A8_Unorm:     channel_count = 4; break;
            case RHI_Format_R10G10B10A2_Unorm:  channel_count = 4; break;
            case RHI_Format_R16G16B16A16_Unorm: channel_count = 4; break;
            case RHI_Format_R16G16B16A16_Snorm: channel_count = 4; break;
            case RHI_Format_R16G16B16A16_Float: channel_count = 4; break;
            case RHI_Format_R32G32B32A32_Float: channel_count = 4; break;
        }

        return channel_count;
    }

    constexpr std::string RhiFormatToString(const RHI_Format result)
    {
        std::string format;

        switch (result)
        {
            case RHI_Format_R8_Unorm:             format = "RHI_Format_R8_Unorm";             break;
            case RHI_Format_R8_Uint:              format = "RHI_Format_R8_Uint";              break;
            case RHI_Format_R16_Unorm:            format = "RHI_Format_R16_Unorm";            break;
            case RHI_Format_R16_Uint:             format = "RHI_Format_R16_Uint";             break;
            case RHI_Format_R16_Float:            format = "RHI_Format_R16_Float";            break;
            case RHI_Format_R32_Uint:             format = "RHI_Format_R32_Uint";             break;
            case RHI_Format_R32_Float:            format = "RHI_Format_R32_Float";            break;
            case RHI_Format_R8G8_Unorm:           format = "RHI_Format_R8G8_Unorm";           break;
            case RHI_Format_R16G16_Float:         format = "RHI_Format_R16G16_Float";         break;
            case RHI_Format_R32G32_Float:         format = "RHI_Format_R32G32_Float";         break;
            case RHI_Format_R11G11B10_Float:      format = "RHI_Format_R11G11B10_Float";      break;
            case RHI_Format_R32G32B32_Float:      format = "RHI_Format_R32G32B32_Float";      break;
            case RHI_Format_R8G8B8A8_Unorm:       format = "RHI_Format_R8G8B8A8_Unorm";       break;
            case RHI_Format_R10G10B10A2_Unorm:    format = "RHI_Format_R10G10B10A2_Unorm";    break;
            case RHI_Format_R16G16B16A16_Unorm:   format = "RHI_Format_R16G16B16A16_Unorm";   break;
            case RHI_Format_R16G16B16A16_Snorm:   format = "RHI_Format_R16G16B16A16_Snorm";   break;
            case RHI_Format_R16G16B16A16_Float:   format = "RHI_Format_R16G16B16A16_Float";   break;
            case RHI_Format_R32G32B32A32_Float:   format = "RHI_Format_R32G32B32A32_Float";   break;
            case RHI_Format_D32_Float:            format = "RHI_Format_D32_Float";            break;
            case RHI_Format_D32_Float_S8X24_Uint: format = "RHI_Format_D32_Float_S8X24_Uint"; break;
            case RHI_Format_BC7:                  format = "RHI_Format_BC7";                  break;
            case RHI_Format_Undefined:            format = "RHI_Format_Undefined";            break;
        }

        return format;
    }
}
