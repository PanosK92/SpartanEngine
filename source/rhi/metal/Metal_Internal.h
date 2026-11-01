/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===================
#include "../RHI_Implementation.h"
//==============================

// one mtl4 queue serves every rhi queue type, metal 4 queues are cheap but residency sets attach per queue
namespace spartan::metal_queue
{
    MTL4::CommandQueue* Get();
    // commits, waits and drawable signals from any thread go through this, their order on the queue matters
    std::mutex& GetMutex();
}

// every allocation the gpu touches must live in a residency set attached to the queue
// static:  buffers, created once and kept for the lifetime of the world
// dynamic: sampled textures, streamed in and out while the world runs
// screen:  render targets and storage textures, recreated when the resolution changes
namespace spartan::metal_residency
{
    enum class Group
    {
        Static,
        Dynamic,
        Screen,
        Count
    };

    void Initialize(MTL4::CommandQueue* queue);
    void Shutdown();
    void Add(Group group, MTL::Allocation* allocation);
    void Remove(MTL::Allocation* allocation);
    // staged adds and removes reach the gpu here, the queue calls it before every commit
    void Commit();
}

// hlsl reaches metal as dxil that metal shader converter lowers against the root signature d3d12 uses,
// so shaders, the top level argument buffer and the descriptor tables all share the d3d12 layout
namespace spartan::metal_bindings
{
    // a descriptor table entry, laid out the way converted shaders read it
    struct Descriptor
    {
        uint64_t gpu_address = 0; // buffers, samplers and acceleration structures
        uint64_t texture_id  = 0; // textures
        uint64_t metadata    = 0; // buffer length, texture min lod or sampler lod bias
    };

    // root parameters in d3d12 order, see D3D12_Pipeline.cpp
    enum Parameter : uint32_t
    {
        cbv_frame,
        push_constants,
        srv_table,
        uav_table,
        material_textures,
        material_parameters,
        light_parameters,
        aabbs,
        draw_data,
        geometry,
        samplers,
        parameter_count
    };

    // buffer indices converted shaders read from
    constexpr uint32_t index_top_level       = 2;
    constexpr uint32_t index_ray_dispatch    = 3; // IRDispatchRaysArgument of the next trace_rays
    constexpr uint32_t index_draw_arguments  = 4;
    constexpr uint32_t index_draw_uniforms   = 5;
    constexpr uint32_t index_vertex_buffer   = 6;
    constexpr uint32_t index_vertex_zero     = 30; // vertex inputs the input layout doesn't provide read zeros from here
    constexpr uint32_t index_stage_in_offset = 11; // vertex attribute n is fetched as attribute 11 + n

    constexpr uint32_t push_constants_size_max = 128;

    void Initialize();
    void Shutdown();
    void* GetRootSignature();               // IRRootSignature, kept opaque so only the shader compiler sees the converter header
    uint32_t GetOffset(Parameter parameter); // byte offset in the top level argument buffer
    uint32_t GetTopLevelSize();
    uint64_t GetBindlessAddress(Parameter parameter);
    uint64_t GetZeroAddress();

    inline Descriptor buffer(MTL::Buffer* buffer, const uint64_t offset, const uint64_t size)
    {
        return { buffer->gpuAddress() + offset, 0, size & 0xffffffff };
    }

    inline Descriptor texture(MTL::Texture* texture)
    {
        return { 0, texture->gpuResourceID()._impl, 0 };
    }

    inline Descriptor sampler(MTL::SamplerState* sampler, const float lod_bias)
    {
        return { sampler->gpuResourceID()._impl, 0, std::bit_cast<uint32_t>(lod_bias) };
    }
}

// what RHI_Shader::GetRhiResource() points to
namespace spartan::metal_shader
{
    struct VertexInput
    {
        std::string semantic; // lowercase semantic plus index, as the converter reports it, e.g. texcoord0
        uint32_t attribute = 0;
    };

    struct Shader
    {
        MTL::Library* library      = nullptr;
        MTL::Function* function    = nullptr;
        MTL::Size threadgroup_size = MTL::Size(1, 1, 1);
        std::vector<VertexInput> vertex_inputs;
    };

    void Destroy(void* shader);
}

// what RHI_Pipeline::GetRhiResource() points to, raster state metal keeps on the encoder travels with the pipeline
namespace spartan::metal_pipeline
{
    struct Pipeline
    {
        MTL::RenderPipelineState* render       = nullptr;
        MTL::ComputePipelineState* compute     = nullptr;
        MTL::DepthStencilState* depth_stencil  = nullptr;
        MTL::PrimitiveType primitive_type      = MTL::PrimitiveTypeTriangle;
        MTL::TriangleFillMode fill_mode        = MTL::TriangleFillModeFill;
        MTL::DepthClipMode depth_clip_mode     = MTL::DepthClipModeClip;
        bool cull_none                         = false; // imgui draws both windings whatever the pass asks for
        float depth_bias                       = 0.0f;
        float depth_bias_slope                 = 0.0f;
        float depth_bias_clamp                 = 0.0f;
        MTL::Size threadgroup_size             = MTL::Size(1, 1, 1);

        // ray tracing, the miss and hit functions the ray generation kernel calls and the records that name them
        MTL::VisibleFunctionTable* function_table = nullptr;
        MTL::Buffer* shader_table                 = nullptr;
    };

    // a shader binding table record, see IRShaderIdentifier, records are raygen, miss, hit
    struct ShaderRecord
    {
        uint64_t intersection_handle = 0;
        uint64_t shader_handle       = 0; // index into the function table, 0 means no shader
        uint64_t static_samplers     = 0;
        uint64_t padding             = 0;
    };

    void Destroy(void* pipeline);
}

namespace spartan::metal_texture
{
    // the part of a texture a binding sees, created on first use and released with the texture
    // layer is rhi_all_mips for every layer, uav views see one mip and see cubes as arrays, the way hlsl declares them
    MTL::Texture* GetView(RHI_Texture* texture, uint32_t mip, uint32_t mip_count, uint32_t layer, bool uav);
}

namespace spartan::metal_swapchain
{
    // the engine's sdr format is rgba8, which a CAMetalLayer can't present, so the layer stores bgra8
    inline MTL::PixelFormat layer_format(const RHI_Format format)
    {
        return format == RHI_Format::R10G10B10A2_Unorm ? MTL::PixelFormatRGB10A2Unorm : MTL::PixelFormatBGRA8Unorm;
    }
}

// the command list records into one encoder at a time, metal 4 encoders cannot overlap on a command buffer
namespace spartan::metal_encoder
{
    enum class Type
    {
        None,
        Render,
        Compute
    };

    struct State
    {
        Type type                           = Type::None;
        MTL4::CommandEncoder* encoder       = nullptr;
        MTL4::ArgumentTable* argument_table = nullptr;
        uint32_t render_pass_width          = 0; // metal rejects scissors that reach outside the attachments
        uint32_t render_pass_height         = 0;
        std::string pass_name               = "unnamed"; // labels new encoders, gpu captures and validation reports show it

        // bindings the next draw or dispatch writes into its top level argument buffer
        std::array<metal_bindings::Descriptor, rhi_max_resource_slots> srv          = {};
        std::array<metal_bindings::Descriptor, rhi_max_resource_slots> uav          = {};
        std::array<uint8_t, metal_bindings::push_constants_size_max> push_constants = {};
        std::array<uint64_t, 2> vertex_buffers                                      = {};
        uint64_t srv_table                                                          = 0; // gpu copy of srv, 0 once srv changes
        uint64_t uav_table                                                          = 0;
        uint64_t cbv_frame                                                          = 0;
        uint64_t top_level                                                          = 0; // the last top level argument buffer written
        uint64_t index_buffer                                                       = 0;
        uint64_t index_buffer_size                                                  = 0;
        MTL::IndexType index_type                                                   = MTL::IndexTypeUInt32;
        metal_pipeline::Pipeline* pipeline                                          = nullptr;
        metal_pipeline::Pipeline* pipeline_bound                                    = nullptr; // what the open encoder has, encoders start with nothing

        // linear memory for argument buffers, rewound when the list begins, the gpu is done with it by then
        std::vector<MTL::Buffer*> upload_buffers;
        uint32_t upload_index  = 0;
        uint64_t upload_offset = 0;

        // metal 4 tracks no hazards, work overlaps unless a barrier orders it, draws and dispatches get one only
        // when the shared resource tracking asks for it, transfers always do since it doesn't see all of them
        bool barrier_pending = false;
        bool first_encoder   = true; // a command buffer starts behind everything committed before it

        // the list's first slot in its shared timestamp heap
        uint32_t timestamp_base = 0;
    };

    // transfer work (copies, fills, clears, blits, uploads, acceleration structure builds) waits for all prior work
    // and makes the next work wait for it, shader work only waits when a barrier is pending
    MTL4::ComputeCommandEncoder* compute(void* cmd_buffer, State& state, bool transfer = true);
    MTL4::RenderCommandEncoder* render(void* cmd_buffer, State& state, MTL4::RenderPassDescriptor* descriptor, bool transfer = true);
    void barrier(State& state);
    void end(State& state);
}

namespace spartan::metal_utility
{
    // retained, the caller releases it, engine threads have no autorelease pool to drain into
    inline NS::String* ns_string(const char* text)
    {
        return NS::String::alloc()->init(text ? text : "", NS::UTF8StringEncoding);
    }

    template<typename T>
    inline void set_label(T* object, const char* name)
    {
        NS::String* label = ns_string(name);
        object->setLabel(label);
        label->release();
    }

    inline MTL::PixelFormat to_pixel_format(const RHI_Format format)
    {
        return metal_format[rhi_format_to_index(format)];
    }
}
