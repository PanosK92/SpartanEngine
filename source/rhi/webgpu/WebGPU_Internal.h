/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====================
#include "../RHI_Implementation.h"
#include "../RHI_PipelineState.h"
#include <array>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <string>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
//================================

namespace spartan
{
    class RHI_Buffer;
    class RHI_Texture;
    class RHI_Shader;
    class RHI_Sampler;

    namespace webgpu
    {
        // hlsl spaces fold into three groups, see the remap in WebGPU_Shader.cpp
        constexpr uint32_t group_pass          = 0; // space 0, t/u/b registers of the pass
        constexpr uint32_t group_bindless      = 1; // spaces 1 to 10
        constexpr uint32_t group_push          = 2; // b1, the push constants
        constexpr uint32_t group_count         = 3;
        constexpr uint32_t push_constants_size = 256; // the uniform binding of b1, also its dynamic offset alignment
        constexpr uint32_t binding_samplers    = 400; // s registers in space 6 and 7, comparison first

        inline WGPUStringView label(const char* text)
        {
            return { text, text ? WGPU_STRLEN : 0 };
        }

        inline WGPUTextureFormat to_format(const RHI_Format format)
        {
            return format < RHI_Format::Max ? webgpu_format[static_cast<uint32_t>(format)] : WGPUTextureFormat_Undefined;
        }

        inline bool is_depth_format(const WGPUTextureFormat format)
        {
            return format == WGPUTextureFormat_Depth16Unorm || format == WGPUTextureFormat_Depth32Float || format == WGPUTextureFormat_Depth32FloatStencil8 || format == WGPUTextureFormat_Depth24Plus || format == WGPUTextureFormat_Depth24PlusStencil8;
        }

        inline bool has_stencil(const WGPUTextureFormat format)
        {
            return format == WGPUTextureFormat_Depth32FloatStencil8 || format == WGPUTextureFormat_Depth24PlusStencil8;
        }

        WGPUQueue queue();
        void process_events();
        bool has_feature(WGPUFeatureName feature);
        bool is_storage_format(WGPUTextureFormat format);
        bool is_filterable_format(WGPUTextureFormat format);
        WGPUTextureSampleType sample_type_of(WGPUTextureFormat format);
        uint64_t frame_index();

        // surfaces don't all offer rgba8, the swapchain records what it got and pipelines target that
        WGPUTextureFormat surface_format();

        // what a shader declares at one binding, the wgsl decides the types, spir-v the sampler pairs
        enum class Kind : uint32_t
        {
            Uniform,
            Storage,
            StorageRead,
            Texture,
            StorageTexture,
            Sampler,
            SamplerComparison
        };

        struct Binding
        {
            uint32_t group                       = 0;
            uint32_t binding                     = 0;
            Kind kind                            = Kind::Uniform;
            WGPUTextureViewDimension dimension   = WGPUTextureViewDimension_2D;
            WGPUTextureSampleType sample_type    = WGPUTextureSampleType_Float;
            WGPUTextureFormat storage_format     = WGPUTextureFormat_Undefined;
            WGPUStorageTextureAccess access      = WGPUStorageTextureAccess_WriteOnly;
            uint64_t visibility                  = 0;
            uint64_t sampler_mask                = 0; // the s registers a texture is sampled with, bit i is binding 400 + i
            uint32_t multisampled                = 0;
            uint32_t filtering                   = 1; // samplers only, resolved when the pipeline layout is built
        };

        struct VertexInput
        {
            char semantic[32]         = {};
            uint32_t location         = 0;
            WGPUVertexFormat format   = WGPUVertexFormat_Float32x4; // read from the zero buffer when the input layout has no match
        };

        struct Shader
        {
            WGPUShaderModule module = nullptr;
            std::vector<Binding> bindings;
            std::vector<VertexInput> vertex_inputs;
            uint32_t fragment_outputs = 0; // bit i is @location(i) of the fragment entry point
            std::string wgsl;              // kept so pipeline variants can patch storage formats
        };

        // wgsl storage textures have a fixed texel format and depth can't bind as filterable float,
        // so a pipeline gets a variant per combination of what is actually bound to group 0
        struct BindingOverride
        {
            uint32_t binding                  = 0;
            WGPUTextureFormat storage_format  = WGPUTextureFormat_Undefined; // storage textures
            WGPUTextureSampleType sample_type = WGPUTextureSampleType_Undefined; // sampled textures
        };

        const char* storage_format_name(WGPUTextureFormat format);
        WGPUShaderModule create_module_variant(const Shader* shader, const std::vector<BindingOverride>& overrides, const char* name);

        enum class PipelineStatus : uint32_t
        {
            Pending,
            Ready,
            Failed
        };

        struct Pipeline
        {
            WGPURenderPipeline render                                   = nullptr;
            WGPUComputePipeline compute                                 = nullptr;
            WGPUPipelineLayout layout                                   = nullptr;
            std::array<WGPUBindGroupLayout, group_count> group_layouts  = {};
            std::array<std::vector<Binding>, group_count> bindings;
            bool uses_zero_buffer                                       = false;
            bool uses_vertex_buffer                                     = false;
            uint32_t zero_buffer_slot                                   = 0;
            bool imgui                                                  = false;

            // creation is asynchronous so a pipeline that fails validation never reaches a command buffer
            std::atomic<PipelineStatus> status                          = PipelineStatus::Pending;
            std::string name;

            // the bindless group only changes when a bindless resource does
            std::mutex bindless_mutex;
            WGPUBindGroup bindless_group                                = nullptr;
            uint64_t bindless_generation                                = 0;

            // variants are owned by the pipeline they were made from
            RHI_PipelineState state;
            std::mutex variants_mutex;
            std::unordered_map<std::string, Pipeline*> variants;
        };

        void destroy_shader(void* resource);
        void destroy_pipeline(void* resource);

        // the base pipeline until the variant is ready, or when it can't be made
        Pipeline* pipeline_variant(Pipeline* base, const std::vector<BindingOverride>& overrides);

        // shared bind group layout of the push constants and the dynamic uniform it points into
        WGPUBindGroupLayout push_constants_layout();

        // empty slots bind these, webgpu has no null descriptors
        constexpr uint64_t dummy_uniform_size = 65536;
        WGPUBuffer dummy_uniform_buffer();
        WGPUBuffer dummy_storage_buffer(bool writable, uint32_t binding, uint64_t& offset, uint64_t& size); // writable ranges never alias
        WGPUBuffer zero_vertex_buffer();
        WGPUTextureView dummy_texture(WGPUTextureViewDimension dimension, WGPUTextureSampleType sample_type);
        WGPUTextureView white_texture();
        WGPUTextureView dummy_storage_texture(WGPUTextureFormat format, WGPUTextureViewDimension dimension, uint32_t binding);
        WGPUSampler dummy_sampler(bool comparison, bool filtering);
        void destroy_dummies();

        // bindless resources, by hlsl binding (t15 space1 is 315 and so on)
        struct BindlessBuffer
        {
            RHI_Buffer* buffer = nullptr;
            uint32_t binding   = 0;
        };
        uint64_t bindless_generation();
        std::vector<BindlessBuffer> bindless_buffers();
        RHI_Sampler* bindless_sampler(uint32_t index); // 0 is the comparison sampler, 1 to 8 the regular ones
        bool bindless_sampler_filtering(uint32_t binding);
        void bindless_invalidate(); // a buffer got a new gpu resource, groups that point at the old one rebuild
        void bindless_reset();

        // views are created on demand per mip, layer and dimension, they live as long as the texture
        WGPUTextureFormat texture_format(const RHI_Texture* texture); // what the texture is stored as, which can be wider than its rhi format
        WGPUTextureView texture_view(RHI_Texture* texture, WGPUTextureViewDimension dimension, uint32_t mip, uint32_t mip_count, uint32_t layer, uint32_t layer_count, bool depth_only);
        void texture_views_release(void* texture);

        // mappable buffers live in a cpu shadow, the queue writes what changed before every submit
        void buffer_register(RHI_Buffer* buffer);
        void buffer_unregister(RHI_Buffer* buffer);
        bool buffer_is_alive(RHI_Buffer* buffer);
        void buffer_mark_dirty(RHI_Buffer* buffer, uint64_t offset, uint64_t size);
        void buffer_flush(RHI_Buffer* buffer, bool touched);
        // copies what the gpu wrote into the cpu shadow, blocks until the queue is done with it
        void buffer_read_back(RHI_Buffer* buffer);
    }
}
