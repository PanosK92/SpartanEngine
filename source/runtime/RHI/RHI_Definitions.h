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

#pragma once

//= INCLUDES ==================
#include <cstdint>
#include <cassert>
#include <limits>
#include "../Rendering/Color.h"
//=============================

// Declarations
namespace spartan
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
    class RHI_Buffer;
    class RHI_ConstantBuffer;
    class RHI_Sampler;
    class RHI_Viewport;
    class RHI_Texture;
    class RHI_Shader;
    class RHI_SyncPrimitive;
    class RHI_AccelerationStructure;
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

    enum class RHI_Format
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
        Image,
        ImageView,
        DescriptorSet,
        DescriptorSetLayout,
        Pipeline,
        PipelineLayout,
        Queue,
        AccelerationStructure,
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
        Image,
        TextureStorage,
        PushConstantBuffer,
        ConstantBuffer,
        StructuredBuffer,
        AccelerationStructure,
        Max
    };

    enum class RHI_Image_Layout
    {
        General,
        Preinitialized,
        Attachment,
        Shading_Rate_Attachment,
        Shader_Read,
        Transfer_Source,
        Transfer_Destination,
        Present_Source,
        Max
    };

    enum RHI_Shader_Type
    {
        Vertex,
        Hull,
        Domain,
        Pixel,
        Compute,
        RayGeneration,
        RayMiss,
        RayHit,
        Max
    };

    static uint32_t rhi_shader_type_to_mask(RHI_Shader_Type type)
    {
        switch (type)
        {
            case RHI_Shader_Type::Vertex:        return 1 << 0;
            case RHI_Shader_Type::Hull:          return 1 << 1;
            case RHI_Shader_Type::Domain:        return 1 << 2;
            case RHI_Shader_Type::Pixel:         return 1 << 3;
            case RHI_Shader_Type::Compute:       return 1 << 4;
            case RHI_Shader_Type::RayGeneration: return 1 << 5;
            case RHI_Shader_Type::RayMiss:       return 1 << 6;
            case RHI_Shader_Type::RayHit:        return 1 << 7;
            default:                             return 0;
        }
    }

    enum class RHI_Device_Bindless_Resource
    {
        // must match order of appearance in common_resources.hlsl
        MaterialTextures,
        MaterialParameters,
        LightParameters,
        Aabbs,
        SamplersComparison,
        SamplersRegular,
        Max
    };

    enum class RHI_BarrierType
    {
        EnsureWriteThenRead,  // RAW: make prior write visible before read (e.g., post-dispatch)
        EnsureReadThenWrite,  // WAR: order read before write (execution dep; e.g., pre-dispatch)
        EnsureWriteThenWrite  // WAW: order prior write before new write (e.g., sequential computes on same UAV)
    };

    // allows specifying barrier scope instead of conservative auto-deduction
    enum class RHI_Barrier_Scope : uint8_t
    {
        Auto,     // deduce from layout/usage (default, conservative)
        Graphics, // vertex/fragment/tessellation stages
        Compute,  // compute stage only
        Transfer, // transfer stage only
        Fragment, // fragment stage only
        All       // all commands (most conservative, explicit)
    };

    // unified barrier description - can represent any barrier type
    struct RHI_Barrier
    {
        enum class Type : uint8_t
        {
            ImageLayout, // layout transition
            ImageSync,   // execution/memory barrier, no layout change
            BufferSync   // buffer memory barrier
        };

        Type type = Type::ImageLayout;

        // scope control - defaults to auto for backwards compatibility
        RHI_Barrier_Scope scope_src = RHI_Barrier_Scope::Auto;
        RHI_Barrier_Scope scope_dst = RHI_Barrier_Scope::Auto;

        // for image barriers
        RHI_Texture* texture       = nullptr;
        void* image                = nullptr; // raw handle for swapchain images
        RHI_Format format          = RHI_Format::Max;
        uint32_t mip_index         = 0;
        uint32_t mip_range         = 1;
        uint32_t array_length      = 1;
        RHI_Image_Layout layout    = RHI_Image_Layout::Max;
        RHI_BarrierType sync_type  = RHI_BarrierType::EnsureWriteThenRead;

        // for buffer barriers
        RHI_Buffer* buffer = nullptr;
        uint64_t offset    = 0;
        uint64_t size      = 0; // 0 = whole buffer

        // factory: texture layout transition
        static RHI_Barrier image_layout(RHI_Texture* tex, RHI_Image_Layout new_layout,
                                        uint32_t mip = std::numeric_limits<uint32_t>::max(), uint32_t range = 0)
        {
            RHI_Barrier b;
            b.type      = Type::ImageLayout;
            b.texture   = tex;
            b.layout    = new_layout;
            b.mip_index = mip;
            b.mip_range = range;
            return b;
        }

        // factory: raw image layout transition (for swapchain etc.)
        static RHI_Barrier image_layout(void* img, RHI_Format fmt, uint32_t mip,
                                        uint32_t range, uint32_t arr_len, RHI_Image_Layout new_layout)
        {
            RHI_Barrier b;
            b.type         = Type::ImageLayout;
            b.image        = img;
            b.format       = fmt;
            b.mip_index    = mip;
            b.mip_range    = range;
            b.array_length = arr_len;
            b.layout       = new_layout;
            return b;
        }

        // factory: texture sync barrier (no layout change)
        static RHI_Barrier image_sync(RHI_Texture* tex, RHI_BarrierType sync)
        {
            RHI_Barrier b;
            b.type      = Type::ImageSync;
            b.texture   = tex;
            b.sync_type = sync;
            return b;
        }

        // factory: buffer sync barrier
        static RHI_Barrier buffer_sync(RHI_Buffer* buf, uint64_t off = 0, uint64_t sz = 0)
        {
            RHI_Barrier b;
            b.type   = Type::BufferSync;
            b.buffer = buf;
            b.offset = off;
            b.size   = sz;
            return b;
        }

        // chainable scope modifiers
        RHI_Barrier& from(RHI_Barrier_Scope scope) { scope_src = scope; return *this; }
        RHI_Barrier& to(RHI_Barrier_Scope scope)   { scope_dst = scope; return *this; }
    };

    static uint64_t rhi_hash_combine(uint64_t a, uint64_t b)
    {
        return a * 31 + b;
    }

    static uint32_t rhi_format_to_bits_per_channel(const RHI_Format format)
    {
        switch (format)
        {
            case RHI_Format::R8_Unorm:              return 8;
            case RHI_Format::R8_Uint:               return 8;
            case RHI_Format::R16_Unorm:             return 16;
            case RHI_Format::R16_Uint:              return 16;
            case RHI_Format::R16_Float:             return 16;
            case RHI_Format::R32_Uint:              return 32;
            case RHI_Format::R32_Float:             return 32;
            case RHI_Format::R8G8_Unorm:            return 8;
            case RHI_Format::R16G16_Float:          return 16;
            case RHI_Format::R32G32_Float:          return 32;
            case RHI_Format::R11G11B10_Float:       return 11;
            case RHI_Format::R32G32B32_Float:       return 32;
            case RHI_Format::R8G8B8A8_Unorm:        return 8;
            case RHI_Format::R10G10B10A2_Unorm:     return 10;
            case RHI_Format::R16G16B16A16_Unorm:    return 16;
            case RHI_Format::R16G16B16A16_Snorm:    return 16;
            case RHI_Format::R16G16B16A16_Float:    return 16;
            case RHI_Format::R32G32B32A32_Float:    return 32;
            case RHI_Format::D16_Unorm:             return 16;
            case RHI_Format::D32_Float:             return 32;
            case RHI_Format::D32_Float_S8X24_Uint:  return 32;
            case RHI_Format::BC1_Unorm:             return 4;
            case RHI_Format::BC3_Unorm:             return 4;
            case RHI_Format::BC5_Unorm:             return 8;
            case RHI_Format::BC7_Unorm:             return 8;
            case RHI_Format::ASTC:                  return 8;
            case RHI_Format::B8R8G8A8_Unorm:        return 8;
            case RHI_Format::Max:                   break;
        }
    
        assert(false && "unhandled rhi_format_to_bits_per_channel() case");
        return 0;
    }

    static uint32_t rhi_to_format_channel_count(const RHI_Format format)
    {
        switch (format)
        {
            case RHI_Format::R8_Unorm:              return 1;
            case RHI_Format::R8_Uint:               return 1;
            case RHI_Format::R16_Unorm:             return 1;
            case RHI_Format::R16_Uint:              return 1;
            case RHI_Format::R16_Float:             return 1;
            case RHI_Format::R32_Uint:              return 1;
            case RHI_Format::R32_Float:             return 1;
            case RHI_Format::R8G8_Unorm:            return 2;
            case RHI_Format::R16G16_Float:          return 2;
            case RHI_Format::R32G32_Float:          return 2;
            case RHI_Format::R11G11B10_Float:       return 3;
            case RHI_Format::R32G32B32_Float:       return 3;
            case RHI_Format::R8G8B8A8_Unorm:        return 4;
            case RHI_Format::R10G10B10A2_Unorm:     return 4;
            case RHI_Format::R16G16B16A16_Unorm:    return 4;
            case RHI_Format::R16G16B16A16_Snorm:    return 4;
            case RHI_Format::R16G16B16A16_Float:    return 4;
            case RHI_Format::R32G32B32A32_Float:    return 4;
            case RHI_Format::D16_Unorm:             return 1;
            case RHI_Format::D32_Float:             return 1;
            case RHI_Format::D32_Float_S8X24_Uint:  return 2;
            case RHI_Format::BC1_Unorm:             return 3;
            case RHI_Format::BC3_Unorm:             return 4;
            case RHI_Format::BC5_Unorm:             return 2;
            case RHI_Format::BC7_Unorm:             return 4;
            case RHI_Format::ASTC:                  return 4;
            case RHI_Format::B8R8G8A8_Unorm:        return 4;
            case RHI_Format::Max:                   break;
        }
    
        assert(false && "unhandled rhi_to_format_channel_count() case");
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
            case RHI_Format::D16_Unorm:            return "RHI_Format_D16_Unorm";
            case RHI_Format::D32_Float:            return "RHI_Format_D32_Float";
            case RHI_Format::D32_Float_S8X24_Uint: return "RHI_Format_D32_Float_S8X24_Uint";
            case RHI_Format::BC1_Unorm:            return "RHI_Format_BC1_Unorm";
            case RHI_Format::BC3_Unorm:            return "RHI_Format_BC3_Unorm";
            case RHI_Format::BC5_Unorm:            return "RHI_Format_BC5_Unorm";
            case RHI_Format::BC7_Unorm:            return "RHI_Format_BC7_Unorm";
            case RHI_Format::ASTC:                 return "RHI_Format_ASTC";
            case RHI_Format::B8R8G8A8_Unorm:       return "RHI_Format_B8R8G8A8_Unorm";
            case RHI_Format::Max:                  return "RHI_Format_Undefined";
            default:                               break;
        }
    
        assert(false && "unhandled rhi_format_to_string() case");
        return "";
    }

    static uint32_t rhi_format_to_index(const RHI_Format format)
    {
        return static_cast<uint32_t>(format);
    }

    // returns the size of a single pixel in bytes for the given format
    static uint32_t rhi_format_to_bytes(const RHI_Format format)
    {
        switch (format)
        {
            case RHI_Format::R8_Unorm:              return 1;
            case RHI_Format::R8_Uint:               return 1;
            case RHI_Format::R16_Unorm:             return 2;
            case RHI_Format::R16_Uint:              return 2;
            case RHI_Format::R16_Float:             return 2;
            case RHI_Format::R32_Uint:              return 4;
            case RHI_Format::R32_Float:             return 4;
            case RHI_Format::R8G8_Unorm:            return 2;
            case RHI_Format::R16G16_Float:          return 4;
            case RHI_Format::R32G32_Float:          return 8;
            case RHI_Format::R11G11B10_Float:       return 4;  // packed 32-bit
            case RHI_Format::R32G32B32_Float:       return 12;
            case RHI_Format::R8G8B8A8_Unorm:        return 4;
            case RHI_Format::R10G10B10A2_Unorm:     return 4;  // packed 32-bit
            case RHI_Format::R16G16B16A16_Unorm:    return 8;
            case RHI_Format::R16G16B16A16_Snorm:    return 8;
            case RHI_Format::R16G16B16A16_Float:    return 8;
            case RHI_Format::R32G32B32A32_Float:    return 16;
            case RHI_Format::D16_Unorm:             return 2;
            case RHI_Format::D32_Float:             return 4;
            case RHI_Format::D32_Float_S8X24_Uint:  return 8;
            case RHI_Format::BC1_Unorm:             return 1;  // ~0.5 bytes/pixel (8 bytes per 4x4 block)
            case RHI_Format::BC3_Unorm:             return 1;  // ~1 byte/pixel (16 bytes per 4x4 block)
            case RHI_Format::BC5_Unorm:             return 1;  // ~1 byte/pixel (16 bytes per 4x4 block)
            case RHI_Format::BC7_Unorm:             return 1;  // ~1 byte/pixel (16 bytes per 4x4 block)
            case RHI_Format::ASTC:                  return 1;  // varies, approximate
            case RHI_Format::B8R8G8A8_Unorm:        return 4;
            case RHI_Format::Max:                   break;
        }
    
        assert(false && "unhandled rhi_format_to_bytes() case");
        return 4; // default fallback
    }

    // shader register slot shifts (required to produce spirv from hlsl)
    // 000-099 is push constant buffer range
    const uint32_t rhi_shader_register_shift_u   = 100;
    const uint32_t rhi_shader_register_shift_b   = 200;
    const uint32_t rhi_shader_register_shift_t   = 300;
    const uint32_t rhi_shader_register_shift_s   = 400;
    const Color    rhi_color_dont_care           = Color(std::numeric_limits<float>::max(), 0.0f, 0.0f, 0.0f);
    const Color    rhi_color_load                = Color(std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f);
    const float    rhi_depth_dont_care           = std::numeric_limits<float>::max();
    const float    rhi_depth_load                = std::numeric_limits<float>::infinity();
    const uint32_t rhi_stencil_dont_care         = std::numeric_limits<uint32_t>::max();
    const uint32_t rhi_stencil_load              = std::numeric_limits<uint32_t>::infinity();
    const uint8_t  rhi_max_render_target_count   = 8;
    const uint8_t  rhi_max_constant_buffer_count = 8;
    const uint32_t rhi_max_array_size            = 4096;
    const uint32_t rhi_max_descriptor_set_count  = 512;
    const uint32_t  rhi_max_mip_count            = 13;
    const uint32_t rhi_all_mips                  = std::numeric_limits<uint32_t>::max();
    const uint32_t rhi_dynamic_offset_empty      = std::numeric_limits<uint32_t>::max();
    const uint32_t rhi_max_buffer_update_size    = 65536; // vkCmdUpdateBuffer has a limit of 65536 bytes
}
