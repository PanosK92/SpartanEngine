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

#pragma once

//= INCLUDES ==================
#include <cstdint>
#include <cassert>
#include <string>
#include <limits>
#include "../Rendering/Color.h"
//=============================

// Declarations
namespace Spartan
{
    class RHI_Context;
    class RHI_Queue;
    class RHI_CommandList;
    class RHI_PipelineState;
    class RHI_Pipeline;
    class RHI_DescriptorSet;
    class RHI_DescriptorSetLayout;
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
        Integrated,
        Discrete,
        Virtual,
        Cpu,
        Max
    };

    enum class RHI_Api_Type
    {
        D3d12,
        Vulkan,
        Max
    };

    enum class RHI_Present_Mode
    {
        Immediate, // Doesn't wait.                  Frames are not dropped. Tearing.    Full on.
        Mailbox,   // Waits for v-blank.             Frames are dropped.     No tearing. Minimizes latency.
        Fifo,      // Waits for v-blank, every time. Frames are not dropped. No tearing. Minimizes stuttering.
    };

    enum class RHI_Queue_Type
    {
        Graphics,
        Compute,
        Copy,
        Max
    };

    enum class RHI_Query_Type
    {
        Timestamp,
        Timestamp_Disjoint
    };

    enum class RHI_PrimitiveTopology
    {
        TriangleList,
        LineList,
        Max
    };

    enum class RHI_CullMode
    {
        Back,
        Front,
        None,
        Max,
    };

    enum class RHI_PolygonMode
    {
        Solid,
        Wireframe,
        Max
    };

    enum class RHI_Filter
    {
        Nearest,
        Linear,
    };

    enum class RHI_Sampler_Address_Mode
    {
        Wrap,
        Mirror,
        Clamp,
        ClampToZero,
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

    enum class RHI_Format : uint32_t // gets serialized so better be explicit
    {
        // R
        R8_Unorm,
        R8_Uint,
        R16_Unorm,
        R16_Uint,
        R16_Float,
        R32_Uint,
        R32_Float,
        // Rg
        R8G8_Unorm,
        R16G16_Float,
        R32G32_Float,
        // Rgb
        R11G11B10_Float,
        R32G32B32_Float,
        // Rgba
        R8G8B8A8_Unorm,
        R10G10B10A2_Unorm,
        R16G16B16A16_Unorm,
        R16G16B16A16_Snorm,
        R16G16B16A16_Float,
        R32G32B32A32_Float,
        // Depth
        D16_Unorm,
        D32_Float,
        D32_Float_S8X24_Uint,
        // Compressed
        BC1_Unorm,
        BC3_Unorm,
        BC5_Unorm,
        BC7_Unorm,
        ASTC,
        // Surface
        B8R8G8A8_Unorm,
        // End
        Max
    };

    enum class RHI_Resource_Type
    {
        Fence,
        Semaphore,
        Shader,
        Sampler,
        QueryPool,
        DeviceMemory,
        Buffer,
        CommandList,
        CommandPool,
        Texture,
        TextureView,
        DescriptorSet,
        DescriptorSetLayout,
        Pipeline,
        PipelineLayout,
        Queue,
        Max
    };

    enum class RHI_Vertex_Type
    {

        Pos,
        PosCol,
        PosUv,
        PosUvNorTan,
        Pos2dUvCol8,
        Max
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
        Max,
        Undefined
    };

    enum class RHI_Descriptor_Type
    {
        Sampler,
        Texture,
        TextureStorage,
        PushConstantBuffer,
        ConstantBuffer,
        StructuredBuffer,
        Max
    };

    enum class RHI_Image_Layout
    {
        General,
        Preinitialized,
        Attachment,
        Shading_Rate_Attachment,
        Shader_Read,
        Shader_Read_Depth,
        Transfer_Source,
        Transfer_Destination,
        Present_Source,
        Max
    };

    enum class RHI_Sync_State
    {
        Idle,
        Submitted,
        Max,
    };

    enum RHI_Shader_Type
    {
        Vertex,
        Hull,
        Domain,
        Pixel,
        Compute,
        Max
    };

    static uint32_t rhi_shader_type_to_mask(RHI_Shader_Type type)
    {
        switch (type)
        {
            case RHI_Shader_Type::Vertex:  return 1 << 0;
            case RHI_Shader_Type::Hull:    return 1 << 1;
            case RHI_Shader_Type::Domain:  return 1 << 2;
            case RHI_Shader_Type::Pixel:   return 1 << 3;
            case RHI_Shader_Type::Compute: return 1 << 4;
            default:                       return 0;
        }
    }

    enum class RHI_Device_Resource
    {
        sampler_comparison,
        sampler_regular,
        textures_material
    };

    static uint64_t rhi_hash_combine(uint64_t seed, uint64_t x)
    {
        // xxHash is probably the best hashing lib out there.
        // To avoid having it as yet another dependency, we'll do this instead:
        return seed ^ (x + 0x9e3779b9 + (seed << 6) + (seed >> 2));
    }

    static uint32_t rhi_format_to_bits_per_channel(const RHI_Format format)
    {
        switch (format)
        {
            case RHI_Format::R8_Unorm:           return 8;
            case RHI_Format::R8_Uint:            return 8;
            case RHI_Format::R16_Unorm:          return 16;
            case RHI_Format::R16_Uint:           return 16;
            case RHI_Format::R16_Float:          return 16;
            case RHI_Format::R32_Uint:           return 32;
            case RHI_Format::R32_Float:          return 32;
            case RHI_Format::R8G8_Unorm:         return 8;
            case RHI_Format::R16G16_Float:       return 16;
            case RHI_Format::R32G32_Float:       return 32;
            case RHI_Format::R32G32B32_Float:    return 32;
            case RHI_Format::R8G8B8A8_Unorm:     return 8;
            case RHI_Format::R16G16B16A16_Unorm: return 16;
            case RHI_Format::R16G16B16A16_Snorm: return 16;
            case RHI_Format::R16G16B16A16_Float: return 16;
            case RHI_Format::R32G32B32A32_Float: return 32;
            case RHI_Format::D32_Float:          return 32;
            case RHI_Format::R11G11B10_Float:    return 11;
        }

        assert(false && "Unsupported format");
        return 0;
    }

    static uint32_t rhi_to_format_channel_count(const RHI_Format format)
    {
        switch (format)
        {
            case RHI_Format::R8_Unorm:           return 1;
            case RHI_Format::R8_Uint:            return 1;
            case RHI_Format::R16_Unorm:          return 1;
            case RHI_Format::R16_Uint:           return 1;
            case RHI_Format::R16_Float:          return 1;
            case RHI_Format::R32_Uint:           return 1;
            case RHI_Format::R32_Float:          return 1;
            case RHI_Format::R8G8_Unorm:         return 2;
            case RHI_Format::R16G16_Float:       return 2;
            case RHI_Format::R32G32_Float:       return 2;
            case RHI_Format::R11G11B10_Float:    return 3;
            case RHI_Format::R32G32B32_Float:    return 3;
            case RHI_Format::R8G8B8A8_Unorm:     return 4;
            case RHI_Format::R10G10B10A2_Unorm:  return 4;
            case RHI_Format::R16G16B16A16_Unorm: return 4;
            case RHI_Format::R16G16B16A16_Snorm: return 4;
            case RHI_Format::R16G16B16A16_Float: return 4;
            case RHI_Format::R32G32B32A32_Float: return 4;
            case RHI_Format::D32_Float:          return 1;
        }

        assert(false && "Unsupported format");
        return 0;
    }

    static const char* rhi_format_to_string(const RHI_Format result)
    {
        switch (result)
        {
            case RHI_Format::R8_Unorm:             return "RHI_Format_R8_Unorm";
            case RHI_Format::R8_Uint:              return "RHI_Format_R8_Uint";
            case RHI_Format::R16_Unorm:            return "RHI_Format_R16_Unorm";
            case RHI_Format::R16_Uint:             return "RHI_Format_R16_Uint";
            case RHI_Format::R16_Float:            return "RHI_Format_R16_Float";
            case RHI_Format::R32_Uint:             return "RHI_Format_R32_Uint";
            case RHI_Format::R32_Float:            return "RHI_Format_R32_Float";
            case RHI_Format::R8G8_Unorm:           return "RHI_Format_R8G8_Unorm";
            case RHI_Format::R16G16_Float:         return "RHI_Format_R16G16_Float";
            case RHI_Format::R32G32_Float:         return "RHI_Format_R32G32_Float";
            case RHI_Format::R11G11B10_Float:      return "RHI_Format_R11G11B10_Float";
            case RHI_Format::R32G32B32_Float:      return "RHI_Format_R32G32B32_Float";
            case RHI_Format::R8G8B8A8_Unorm:       return "RHI_Format_R8G8B8A8_Unorm";
            case RHI_Format::R10G10B10A2_Unorm:    return "RHI_Format_R10G10B10A2_Unorm";
            case RHI_Format::R16G16B16A16_Unorm:   return "RHI_Format_R16G16B16A16_Unorm";
            case RHI_Format::R16G16B16A16_Snorm:   return "RHI_Format_R16G16B16A16_Snorm";
            case RHI_Format::R16G16B16A16_Float:   return "RHI_Format_R16G16B16A16_Float";
            case RHI_Format::R32G32B32A32_Float:   return "RHI_Format_R32G32B32A32_Float";
            case RHI_Format::D32_Float:            return "RHI_Format_D32_Float";
            case RHI_Format::D32_Float_S8X24_Uint: return "RHI_Format_D32_Float_S8X24_Uint";
            case RHI_Format::BC7_Unorm:            return "RHI_Format_BC7";
            case RHI_Format::Max:                  return "RHI_Format_Undefined";
        }

        assert(false && "Unsupported format");
        return "";
    }

    static uint32_t rhi_format_to_index(const RHI_Format format)
    {
        return static_cast<uint32_t>(format);
    }

    // shader register slot shifts (required to produce spirv from hlsl)
    // 000-099 is push constant buffer range
    const uint32_t rhi_shader_shift_register_u   = 100;
    const uint32_t rhi_shader_shift_register_b   = 200;
    const uint32_t rhi_shader_shift_register_t   = 300;
    const uint32_t rhi_shader_shift_register_s   = 400;
    const Color    rhi_color_dont_care           = Color(std::numeric_limits<float>::max(), 0.0f, 0.0f, 0.0f);
    const Color    rhi_color_load                = Color(std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f);
    const float    rhi_depth_dont_care           = std::numeric_limits<float>::max();
    const float    rhi_depth_load                = std::numeric_limits<float>::infinity();
    const uint32_t rhi_stencil_dont_care         = std::numeric_limits<uint32_t>::max();
    const uint32_t rhi_stencil_load              = std::numeric_limits<uint32_t>::infinity();
    const uint8_t  rhi_max_render_target_count   = 8;
    const uint8_t  rhi_max_constant_buffer_count = 8;
    const uint32_t rhi_max_array_size            = 16384;
    const uint32_t rhi_max_array_size_lights     = 128;
    const uint32_t rhi_max_descriptor_set_count  = 512;
    const uint8_t  rhi_max_mip_count             = 13;
    const uint32_t rhi_max_queries_occlusion     = 4096;
    const uint32_t rhi_max_queries_timestamps    = 512;
    const uint32_t rhi_all_mips                  = std::numeric_limits<uint32_t>::max();
    const uint32_t rhi_dynamic_offset_empty      = std::numeric_limits<uint32_t>::max();
}
