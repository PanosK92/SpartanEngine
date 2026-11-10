/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===============================
#include "pch.h"
#include "../RHI_CommandList.h"
#include "../RHI_Device.h"
#include "../RHI_Queue.h"
#include "../RHI_Pipeline.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_SwapChain.h"
#include "../RHI_SyncPrimitive.h"
#include "../RHI_Texture.h"
#include "../RHI_Buffer.h"
#include "../RHI_Sampler.h"
#include "../RHI_AccelerationStructure.h"
#include "../../profiling/Profiler.h"
#include "WebGPU_Internal.h"
#include <condition_variable>
#include <unordered_set>
//==========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        // what a pass binds at one binding of group 0, resolved against the pipeline layout at the draw
        struct Resource
        {
            enum class Type : uint8_t
            {
                None,
                Texture,
                Buffer,
                ConstantBuffer
            };

            Type type            = Type::None;
            RHI_Texture* texture = nullptr;
            uint32_t mip         = 0;
            uint32_t mip_count   = 0;
            uint32_t layer       = rhi_all_mips;
            RHI_Buffer* buffer   = nullptr;
            uint64_t offset      = 0;
            uint64_t size        = 0;
        };

        // cpu memory that reaches the gpu with one queue write when the list is submitted
        struct Chunk
        {
            WGPUBuffer buffer        = nullptr;
            WGPUBindGroup push_group = nullptr;
            vector<uint8_t> data;
            uint64_t used            = 0;
        };

        struct State
        {
            WGPUCommandEncoder encoder           = nullptr;
            WGPURenderPassEncoder render         = nullptr;
            WGPUComputePassEncoder compute       = nullptr;
            webgpu::Pipeline* pipeline           = nullptr;
            webgpu::Pipeline* pipeline_bound     = nullptr;
            webgpu::Pipeline* pipeline_variant   = nullptr; // what pipeline resolved to for the bound textures
            uint32_t pass_width                  = 0;
            uint32_t pass_height                 = 0;
            array<RHI_Texture*, rhi_max_render_target_count + 1> attachments = {};

            // group 0, indexed by binding
            array<Resource, 512> resources;
            bool resources_dirty                 = true;

            // group 2
            array<uint8_t, webgpu::push_constants_size> push_data = {};
            bool push_dirty                      = true;
            uint32_t push_chunk                  = 0;
            uint32_t push_offset                 = 0;

            // input assembly
            WGPUBuffer vertex_buffer             = nullptr;
            uint64_t vertex_size                 = 0;
            WGPUBuffer index_buffer              = nullptr;
            uint64_t index_size                  = 0;
            WGPUIndexFormat index_format         = WGPUIndexFormat_Uint32;

            // upload ring
            vector<Chunk> chunks;
            uint32_t chunk_index                 = 0;

            // buffers whose cpu shadow has to reach the gpu before this list runs
            unordered_set<RHI_Buffer*> touched;

            // readback buffers the gpu writes, their cpu shadow is refreshed once the list is submitted
            vector<RHI_Buffer*> readbacks;
        };

        State& get_state(void* rhi_state)
        {
            return *static_cast<State*>(rhi_state);
        }

        mutex mutex_submit;

        uint64_t max_storage_binding_size()
        {
            static uint64_t size = 0;
            if (size == 0)
            {
                WGPULimits limits = WGPU_LIMITS_INIT;
                wgpuDeviceGetLimits(RHI_Context::device, &limits);
                size = limits.maxStorageBufferBindingSize & ~3ull;
            }
            return size;
        }

        // ring allocation, the alignment covers both copy offsets (4) and dynamic uniform offsets (256)
        uint8_t* allocate(State& state, const uint64_t size, const uint64_t alignment, uint32_t& chunk_index, uint64_t& offset)
        {
            const uint64_t chunk_size = 4 * 1024 * 1024;
            while (true)
            {
                if (state.chunk_index < state.chunks.size())
                {
                    Chunk& chunk           = state.chunks[state.chunk_index];
                    const uint64_t aligned = (chunk.used + alignment - 1) & ~(alignment - 1);
                    if (aligned + size <= chunk.data.size())
                    {
                        chunk_index = state.chunk_index;
                        offset      = aligned;
                        chunk.used  = aligned + size;
                        return chunk.data.data() + aligned;
                    }

                    state.chunk_index++;
                    continue;
                }

                const uint64_t buffer_size = max(chunk_size, (size + 255) & ~255ull);
                Chunk chunk;
                chunk.data.resize(buffer_size);

                WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
                descriptor.label                = webgpu::label("upload_ring");
                descriptor.size                 = buffer_size;
                descriptor.usage                = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
                chunk.buffer                    = wgpuDeviceCreateBuffer(RHI_Context::device, &descriptor);

                WGPUBindGroupEntry entry      = WGPU_BIND_GROUP_ENTRY_INIT;
                entry.binding                 = 0;
                entry.buffer                  = chunk.buffer;
                entry.offset                  = 0;
                entry.size                    = webgpu::push_constants_size;
                WGPUBindGroupDescriptor group = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
                group.label                   = webgpu::label("push_constants");
                group.layout                  = webgpu::push_constants_layout();
                group.entryCount              = 1;
                group.entries                 = &entry;
                chunk.push_group              = wgpuDeviceCreateBindGroup(RHI_Context::device, &group);

                state.chunks.push_back(move(chunk));
            }
        }

        void flush_chunks(State& state)
        {
            for (Chunk& chunk : state.chunks)
            {
                if (chunk.used == 0)
                    continue;

                wgpuQueueWriteBuffer(webgpu::queue(), chunk.buffer, 0, chunk.data.data(), (chunk.used + 3) & ~3ull);
            }
        }

        void reset_chunks(State& state)
        {
            for (Chunk& chunk : state.chunks)
            {
                chunk.used = 0;
            }
            state.chunk_index = 0;
        }

        void release_chunks(State& state)
        {
            for (Chunk& chunk : state.chunks)
            {
                wgpuBindGroupRelease(chunk.push_group);
                wgpuBufferRelease(chunk.buffer);
            }
            state.chunks.clear();
        }

        void end_compute(State& state)
        {
            if (state.compute)
            {
                wgpuComputePassEncoderEnd(state.compute);
                wgpuComputePassEncoderRelease(state.compute);
                state.compute        = nullptr;
                state.pipeline_bound = nullptr;
            }
        }

        void end_render(State& state)
        {
            if (state.render)
            {
                wgpuRenderPassEncoderEnd(state.render);
                wgpuRenderPassEncoderRelease(state.render);
                state.render         = nullptr;
                state.pipeline_bound = nullptr;
                state.attachments.fill(nullptr);
            }
        }

        WGPUTextureViewDimension texture_dimension(RHI_Texture* texture)
        {
            switch (texture->GetType())
            {
                case RHI_Texture_Type::Type3D:      return WGPUTextureViewDimension_3D;
                case RHI_Texture_Type::Type2DArray: return WGPUTextureViewDimension_2DArray;
                case RHI_Texture_Type::TypeCube:    return WGPUTextureViewDimension_Cube;
                default:                            return WGPUTextureViewDimension_2D;
            }
        }

        // a view that satisfies the layout's dimension, or null when the texture can't
        WGPUTextureView view_for(RHI_Texture* texture, const WGPUTextureViewDimension dimension, uint32_t mip, uint32_t mip_count, const uint32_t layer)
        {
            const uint32_t mips   = max(1u, texture->GetResidentMipCount());
            const uint32_t layers = max(1u, texture->GetArrayLength());
            const bool is_3d      = texture->GetType() == RHI_Texture_Type::Type3D;
            if ((dimension == WGPUTextureViewDimension_3D) != is_3d)
                return nullptr;

            mip       = min(mip, mips - 1);
            mip_count = min(max(mip_count, 1u), mips - mip);

            uint32_t layer_first = 0;
            uint32_t layer_count = 1;
            switch (dimension)
            {
                case WGPUTextureViewDimension_2D:
                    layer_first = layer != rhi_all_mips ? min(layer, layers - 1) : 0;
                    break;
                case WGPUTextureViewDimension_2DArray:
                    layer_first = layer != rhi_all_mips ? min(layer, layers - 1) : 0;
                    layer_count = layer != rhi_all_mips ? 1 : layers;
                    break;
                case WGPUTextureViewDimension_Cube:
                    if (layers < 6 || texture->GetResidentWidth() != texture->GetResidentHeight())
                        return nullptr;
                    layer_count = 6;
                    break;
                case WGPUTextureViewDimension_CubeArray:
                    if (layers < 6 || layers % 6 != 0)
                        return nullptr;
                    layer_count = layers;
                    break;
                default:
                    break;
            }

            return webgpu::texture_view(texture, dimension, mip, mip_count, layer_first, layer_count, texture->IsDepthStencilFormat());
        }

        bool sample_type_compatible(const WGPUTextureSampleType layout, const WGPUTextureSampleType format)
        {
            if (layout == format)
                return true;

            if (layout == WGPUTextureSampleType_UnfilterableFloat)
                return format == WGPUTextureSampleType_Float || format == WGPUTextureSampleType_Depth;

            return false;
        }

        bool is_attachment(const State& state, const RHI_Texture* texture)
        {
            for (const RHI_Texture* attachment : state.attachments)
            {
                if (attachment == texture)
                    return true;
            }
            return false;
        }

        WGPUBindGroupEntry entry_for_texture(const State& state, const webgpu::Binding& binding, const Resource& resource)
        {
            WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
            entry.binding            = binding.binding;

            RHI_Texture* texture     = resource.type == Resource::Type::Texture ? resource.texture : nullptr;
            WGPUTexture raw          = texture ? static_cast<WGPUTexture>(texture->GetRhiResource()) : nullptr;
            if (raw && !is_attachment(state, texture))
            {
                const WGPUTextureFormat format = webgpu::texture_format(texture);
                const WGPUTextureUsage usage   = wgpuTextureGetUsage(raw);
                if (binding.kind == webgpu::Kind::Texture && (usage & WGPUTextureUsage_TextureBinding) && sample_type_compatible(binding.sample_type, webgpu::sample_type_of(format)))
                {
                    entry.textureView = view_for(texture, binding.dimension, resource.mip, resource.mip_count, resource.layer);
                }
                else if (binding.kind == webgpu::Kind::StorageTexture && (usage & WGPUTextureUsage_StorageBinding) && format == binding.storage_format)
                {
                    entry.textureView = view_for(texture, binding.dimension, resource.mip, 1, resource.layer);
                }

                // a bound texture the layout can't take silently reads or writes a dummy, say so once per texture and binding
                if (!entry.textureView)
                {
                    static mutex mutex_logged;
                    static unordered_set<string> logged;
                    const string key = texture->GetObjectName() + "_" + to_string(binding.binding);
                    lock_guard<mutex> lock(mutex_logged);
                    if (logged.insert(key).second)
                    {
                        SP_LOG_WARNING("%s can't bind to %u (%s, texture format %d sample %d, layout storage format %d sample %d dimension %d, usage 0x%llx)",
                            texture->GetObjectName().c_str(), binding.binding, binding.kind == webgpu::Kind::Texture ? "texture" : "storage texture",
                            static_cast<int>(format), static_cast<int>(webgpu::sample_type_of(format)), static_cast<int>(binding.storage_format), static_cast<int>(binding.sample_type), static_cast<int>(binding.dimension), static_cast<unsigned long long>(usage));
                    }
                }
            }

            if (!entry.textureView)
            {
                entry.textureView = binding.kind == webgpu::Kind::Texture ? webgpu::dummy_texture(binding.dimension, binding.sample_type) : webgpu::dummy_storage_texture(binding.storage_format, binding.dimension, binding.binding);
            }
            return entry;
        }

        WGPUBindGroupEntry entry_for_buffer(const webgpu::Binding& binding, RHI_Buffer* buffer, const uint64_t offset, const uint64_t size)
        {
            WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
            entry.binding            = binding.binding;

            WGPUBuffer raw = buffer ? static_cast<WGPUBuffer>(buffer->GetRhiResource()) : nullptr;
            if (raw)
            {
                const WGPUBufferUsage usage = wgpuBufferGetUsage(raw);
                const uint64_t total        = wgpuBufferGetSize(raw);
                if (binding.kind == webgpu::Kind::Uniform && (usage & WGPUBufferUsage_Uniform) && offset < total)
                {
                    entry.buffer = raw;
                    entry.offset = offset;
                    entry.size   = min(size ? size : total - offset, min<uint64_t>(total - offset, webgpu::dummy_uniform_size));
                }
                else if (binding.kind != webgpu::Kind::Uniform && (usage & WGPUBufferUsage_Storage) && offset < total)
                {
                    entry.buffer = raw;
                    entry.offset = offset;
                    entry.size   = min((size ? size : total - offset) & ~3ull, max_storage_binding_size());
                }
            }

            if (!entry.buffer)
            {
                if (binding.kind == webgpu::Kind::Uniform)
                {
                    entry.buffer = webgpu::dummy_uniform_buffer();
                    entry.offset = 0;
                    entry.size   = webgpu::dummy_uniform_size;
                }
                else
                {
                    uint64_t dummy_offset = 0;
                    uint64_t dummy_size   = 0;
                    entry.buffer          = webgpu::dummy_storage_buffer(binding.kind == webgpu::Kind::Storage, binding.binding, dummy_offset, dummy_size);
                    entry.offset          = dummy_offset;
                    entry.size            = dummy_size;
                }
            }
            return entry;
        }

        WGPUBindGroupEntry entry_for_sampler(const webgpu::Binding& binding, RHI_Sampler* sampler)
        {
            WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
            entry.binding            = binding.binding;
            const bool comparison    = binding.kind == webgpu::Kind::SamplerComparison;
            if (sampler && sampler->GetRhiResource() && sampler->GetComparisonEnabled() == comparison)
            {
                // a non filtering layout only takes non filtering samplers
                if (comparison || binding.filtering || !webgpu::bindless_sampler_filtering(binding.binding))
                {
                    entry.sampler = static_cast<WGPUSampler>(sampler->GetRhiResource());
                }
            }

            if (!entry.sampler)
            {
                entry.sampler = webgpu::dummy_sampler(comparison, binding.filtering != 0);
            }
            return entry;
        }

        WGPUBindGroup create_group(webgpu::Pipeline* pipeline, const uint32_t group, const vector<WGPUBindGroupEntry>& entries)
        {
            WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            descriptor.layout                  = pipeline->group_layouts[group];
            descriptor.entryCount              = entries.size();
            descriptor.entries                 = entries.data();
            return wgpuDeviceCreateBindGroup(RHI_Context::device, &descriptor);
        }

        WGPUBindGroup create_pass_group(State& state, webgpu::Pipeline* pipeline)
        {
            vector<WGPUBindGroupEntry> entries;
            entries.reserve(pipeline->bindings[webgpu::group_pass].size());
            for (const webgpu::Binding& binding : pipeline->bindings[webgpu::group_pass])
            {
                const Resource& resource = binding.binding < state.resources.size() ? state.resources[binding.binding] : Resource();
                switch (binding.kind)
                {
                    case webgpu::Kind::Texture:
                    case webgpu::Kind::StorageTexture:
                        entries.push_back(entry_for_texture(state, binding, resource));
                        break;
                    case webgpu::Kind::Uniform:
                    case webgpu::Kind::Storage:
                    case webgpu::Kind::StorageRead:
                    {
                        const bool is_buffer = resource.type == Resource::Type::Buffer || resource.type == Resource::Type::ConstantBuffer;
                        entries.push_back(entry_for_buffer(binding, is_buffer ? resource.buffer : nullptr, resource.offset, resource.size));
                        break;
                    }
                    case webgpu::Kind::Sampler:
                    case webgpu::Kind::SamplerComparison:
                        entries.push_back(entry_for_sampler(binding, nullptr));
                        break;
                }
            }
            return create_group(pipeline, webgpu::group_pass, entries);
        }

        // bindless resources only change when the device says so, the group lives with the pipeline
        WGPUBindGroup get_bindless_group(webgpu::Pipeline* pipeline)
        {
            lock_guard<mutex> lock(pipeline->bindless_mutex);
            const uint64_t generation = webgpu::bindless_generation();
            if (pipeline->bindless_group && pipeline->bindless_generation == generation)
                return pipeline->bindless_group;

            const vector<webgpu::BindlessBuffer> buffers = webgpu::bindless_buffers();
            vector<WGPUBindGroupEntry> entries;
            entries.reserve(pipeline->bindings[webgpu::group_bindless].size());
            for (const webgpu::Binding& binding : pipeline->bindings[webgpu::group_bindless])
            {
                switch (binding.kind)
                {
                    case webgpu::Kind::Texture:
                    {
                        // the material textures are the only bindless textures, one array with a layer per texture
                        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
                        entry.binding            = binding.binding;
                        WGPUTextureView layers   = binding.binding == webgpu::binding_material_textures && binding.dimension == WGPUTextureViewDimension_2DArray ? webgpu::material_texture_layers() : nullptr;
                        entry.textureView        = layers ? layers : webgpu::dummy_texture(binding.dimension, binding.sample_type);
                        entries.push_back(entry);
                        break;
                    }
                    case webgpu::Kind::StorageTexture:
                        entries.push_back(entry_for_texture(State(), binding, Resource()));
                        break;
                    case webgpu::Kind::Uniform:
                    case webgpu::Kind::Storage:
                    case webgpu::Kind::StorageRead:
                    {
                        RHI_Buffer* buffer = nullptr;
                        for (const webgpu::BindlessBuffer& bindless : buffers)
                        {
                            if (bindless.binding == binding.binding)
                            {
                                buffer = bindless.buffer;
                            }
                        }
                        entries.push_back(entry_for_buffer(binding, buffer, 0, 0));
                        break;
                    }
                    case webgpu::Kind::Sampler:
                    case webgpu::Kind::SamplerComparison:
                        entries.push_back(entry_for_sampler(binding, binding.binding >= webgpu::binding_samplers ? webgpu::bindless_sampler(binding.binding - webgpu::binding_samplers) : nullptr));
                        break;
                }
            }

            if (pipeline->bindless_group)
            {
                wgpuBindGroupRelease(pipeline->bindless_group);
            }
            pipeline->bindless_group      = create_group(pipeline, webgpu::group_bindless, entries);
            pipeline->bindless_generation = generation;
            return pipeline->bindless_group;
        }

        // the variant whose storage formats and sample types match the textures bound to group 0
        webgpu::Pipeline* select_variant(const State& state, webgpu::Pipeline* base)
        {
            vector<webgpu::BindingOverride> overrides;
            for (const webgpu::Binding& binding : base->bindings[webgpu::group_pass])
            {
                if ((binding.kind != webgpu::Kind::Texture && binding.kind != webgpu::Kind::StorageTexture) || binding.binding >= state.resources.size())
                    continue;

                const Resource& resource = state.resources[binding.binding];
                RHI_Texture* texture     = resource.type == Resource::Type::Texture ? resource.texture : nullptr;
                WGPUTexture raw          = texture ? static_cast<WGPUTexture>(texture->GetRhiResource()) : nullptr;
                if (!raw || is_attachment(state, texture))
                    continue;

                const WGPUTextureFormat format = webgpu::texture_format(texture);
                const WGPUTextureUsage usage   = wgpuTextureGetUsage(raw);
                webgpu::BindingOverride entry;
                entry.binding = binding.binding;
                // sampled textures don't get variants, a filterable binding is one the shader samples with a filtering sampler
                if (binding.kind == webgpu::Kind::StorageTexture && (usage & WGPUTextureUsage_StorageBinding) && format != binding.storage_format && webgpu::storage_format_name(format))
                {
                    entry.storage_format = format;
                    overrides.push_back(entry);
                }
            }

            return overrides.empty() ? base : webgpu::pipeline_variant(base, overrides);
        }

        webgpu::Pipeline* ready_pipeline(RHI_Pipeline* pipeline)
        {
            webgpu::Pipeline* resource = pipeline ? static_cast<webgpu::Pipeline*>(pipeline->GetRhiResource()) : nullptr;
            if (!resource || resource->status.load(memory_order_acquire) != webgpu::PipelineStatus::Ready)
                return nullptr;

            return resource;
        }
    }

    // a fullscreen triangle that samples the source, copies can't scale or change format
    namespace webgpu_blit
    {
        const char* source = R"(
            @group(0) @binding(0) var source_texture : texture_2d<f32>;
            @group(0) @binding(1) var source_sampler : sampler;

            struct vertex_output
            {
                @builtin(position) position : vec4f,
                @location(0) uv             : vec2f,
            };

            @vertex fn blit_vs(@builtin(vertex_index) id : u32) -> vertex_output
            {
                var output : vertex_output;
                output.uv       = vec2f(f32((id << 1u) & 2u), f32(id & 2u));
                output.position = vec4f(output.uv * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0), 0.0, 1.0);
                return output;
            }

            @fragment fn blit_ps(input : vertex_output) -> @location(0) vec4f
            {
                return textureSampleLevel(source_texture, source_sampler, input.uv, 0.0);
            }

            @fragment fn blit_load_ps(input : vertex_output) -> @location(0) vec4f
            {
                let size = vec2f(textureDimensions(source_texture));
                return textureLoad(source_texture, vec2i(input.uv * size), 0);
            }
        )";

        struct Pipeline
        {
            WGPURenderPipeline pipeline = nullptr;
            WGPUBindGroupLayout layout  = nullptr;
        };

        WGPUShaderModule module = nullptr;
        WGPUSampler sampler_linear = nullptr;
        WGPUSampler sampler_point  = nullptr;
        unordered_map<uint64_t, Pipeline> pipelines;
        mutex mutex_pipelines;

        const Pipeline* get_pipeline(const WGPUTextureFormat format, const bool filterable)
        {
            lock_guard<mutex> lock(mutex_pipelines);
            const uint64_t key = (static_cast<uint64_t>(format) << 1) | (filterable ? 1 : 0);
            auto it            = pipelines.find(key);
            if (it != pipelines.end())
                return &it->second;

            if (!module)
            {
                WGPUShaderSourceWGSL wgsl          = WGPU_SHADER_SOURCE_WGSL_INIT;
                wgsl.code                          = webgpu::label(source);
                WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
                descriptor.nextInChain             = &wgsl.chain;
                descriptor.label                   = webgpu::label("blit");
                module                             = wgpuDeviceCreateShaderModule(RHI_Context::device, &descriptor);

                WGPUSamplerDescriptor sampler = WGPU_SAMPLER_DESCRIPTOR_INIT;
                sampler.magFilter             = WGPUFilterMode_Linear;
                sampler.minFilter             = WGPUFilterMode_Linear;
                sampler_linear                = wgpuDeviceCreateSampler(RHI_Context::device, &sampler);
                sampler.magFilter             = WGPUFilterMode_Nearest;
                sampler.minFilter             = WGPUFilterMode_Nearest;
                sampler_point                 = wgpuDeviceCreateSampler(RHI_Context::device, &sampler);
            }

            WGPUBindGroupLayoutEntry entries[2] = { WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT };
            entries[0].binding                  = 0;
            entries[0].visibility               = WGPUShaderStage_Fragment;
            entries[0].texture.sampleType       = filterable ? WGPUTextureSampleType_Float : WGPUTextureSampleType_UnfilterableFloat;
            entries[0].texture.viewDimension    = WGPUTextureViewDimension_2D;
            entries[1].binding                  = 1;
            entries[1].visibility               = WGPUShaderStage_Fragment;
            entries[1].sampler.type             = filterable ? WGPUSamplerBindingType_Filtering : WGPUSamplerBindingType_NonFiltering;

            WGPUBindGroupLayoutDescriptor layout_descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            layout_descriptor.entryCount                    = 2;
            layout_descriptor.entries                       = entries;
            Pipeline pipeline;
            pipeline.layout = wgpuDeviceCreateBindGroupLayout(RHI_Context::device, &layout_descriptor);

            WGPUPipelineLayoutDescriptor pipeline_layout_descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
            pipeline_layout_descriptor.bindGroupLayoutCount         = 1;
            pipeline_layout_descriptor.bindGroupLayouts             = &pipeline.layout;
            WGPUPipelineLayout pipeline_layout                      = wgpuDeviceCreatePipelineLayout(RHI_Context::device, &pipeline_layout_descriptor);

            WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
            target.format               = format;
            WGPUFragmentState fragment  = WGPU_FRAGMENT_STATE_INIT;
            fragment.module             = module;
            fragment.entryPoint         = webgpu::label(filterable ? "blit_ps" : "blit_load_ps");
            fragment.targetCount        = 1;
            fragment.targets            = &target;

            WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
            descriptor.label                        = webgpu::label("blit");
            descriptor.layout                       = pipeline_layout;
            descriptor.vertex.module                = module;
            descriptor.vertex.entryPoint            = webgpu::label("blit_vs");
            descriptor.primitive.topology           = WGPUPrimitiveTopology_TriangleList;
            descriptor.fragment                     = &fragment;
            pipeline.pipeline                       = wgpuDeviceCreateRenderPipeline(RHI_Context::device, &descriptor);
            wgpuPipelineLayoutRelease(pipeline_layout);

            return &(pipelines[key] = pipeline);
        }

        void destroy()
        {
            lock_guard<mutex> lock(mutex_pipelines);
            for (auto& [key, pipeline] : pipelines)
            {
                wgpuRenderPipelineRelease(pipeline.pipeline);
                wgpuBindGroupLayoutRelease(pipeline.layout);
            }
            pipelines.clear();

            if (module)
            {
                wgpuShaderModuleRelease(module);
                wgpuSamplerRelease(sampler_linear);
                wgpuSamplerRelease(sampler_point);
                module         = nullptr;
                sampler_linear = nullptr;
                sampler_point  = nullptr;
            }
        }

        // draws the source into the destination view, the caller has ended any open pass
        bool draw(State& state, WGPUTextureView source, const WGPUTextureFormat source_format, WGPUTextureView destination, const WGPUTextureFormat destination_format)
        {
            const WGPUTextureSampleType sample_type = webgpu::sample_type_of(source_format);
            if (!source || !destination || (sample_type != WGPUTextureSampleType_Float && sample_type != WGPUTextureSampleType_UnfilterableFloat))
                return false;

            const bool filterable    = sample_type == WGPUTextureSampleType_Float;
            const Pipeline* pipeline = get_pipeline(destination_format, filterable);
            if (!pipeline->pipeline)
                return false;

            WGPUBindGroupEntry entries[2] = { WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT };
            entries[0].binding            = 0;
            entries[0].textureView        = source;
            entries[1].binding            = 1;
            entries[1].sampler            = filterable ? sampler_linear : sampler_point;
            WGPUBindGroupDescriptor group_descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            group_descriptor.layout                  = pipeline->layout;
            group_descriptor.entryCount              = 2;
            group_descriptor.entries                 = entries;
            WGPUBindGroup group                      = wgpuDeviceCreateBindGroup(RHI_Context::device, &group_descriptor);

            WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            attachment.view                          = destination;
            attachment.loadOp                        = WGPULoadOp_Clear;
            attachment.storeOp                       = WGPUStoreOp_Store;
            attachment.clearValue                    = { 0.0, 0.0, 0.0, 1.0 };
            WGPURenderPassDescriptor pass_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            pass_descriptor.label                    = webgpu::label("blit");
            pass_descriptor.colorAttachmentCount     = 1;
            pass_descriptor.colorAttachments         = &attachment;

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(state.encoder, &pass_descriptor);
            wgpuRenderPassEncoderSetPipeline(pass, pipeline->pipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            wgpuBindGroupRelease(group);
            return true;
        }
    }

    RHI_CommandList::RHI_CommandList(RHI_Queue* queue, void* cmd_pool, const char* name)
    {
        SP_ASSERT(cmd_pool != nullptr);
        SP_ASSERT(queue != nullptr);

        m_rhi_cmd_pool_resource = cmd_pool;
        m_queue                 = queue;
        m_object_name           = name;
        m_rhi_state             = new State();

        m_state = RHI_CommandListState::Idle;
        m_rendering_complete_semaphore_timeline = make_shared<RHI_SyncPrimitive>(RHI_SyncPrimitive_Type::SemaphoreTimeline, (string(name) + "_timeline").c_str());
    }

    RHI_CommandList::~RHI_CommandList()
    {
        if (m_state == RHI_CommandListState::Submitted)
        {
            WaitForExecution();
        }

        ReleasePendingWork();

        State* state = static_cast<State*>(m_rhi_state);
        end_render(*state);
        end_compute(*state);
        if (state->encoder)
        {
            wgpuCommandEncoderRelease(state->encoder);
        }
        release_chunks(*state);
        delete state;
        m_rhi_state    = nullptr;
        m_rhi_resource = nullptr;
    }

    void RHI_CommandList::Begin()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Idle);

        State& state = get_state(m_rhi_state);
        WGPUCommandEncoderDescriptor descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
        descriptor.label                        = webgpu::label(m_object_name.c_str());
        state.encoder                           = wgpuDeviceCreateCommandEncoder(RHI_Context::device, &descriptor);
        m_rhi_resource                          = state.encoder;

        ResetTrackedResources();

        ReadbackTimestampsForProfiler();
        m_timestamp_sample.reset();
        m_timestamp_index = 0;

        // the queue writes of the previous recording are ordered before anything submitted after them
        reset_chunks(state);
        state.resources.fill({});
        state.resources_dirty  = true;
        state.push_dirty       = true;
        state.pipeline         = nullptr;
        state.pipeline_bound   = nullptr;
        state.pipeline_variant = nullptr;
        state.vertex_buffer    = nullptr;
        state.index_buffer     = nullptr;
        state.attachments.fill(nullptr);
        state.touched.clear();
        state.readbacks.clear();

        m_state                = RHI_CommandListState::Recording;
        m_buffer_id_vertex     = 0;
        m_buffer_id_index      = 0;
        m_render_pass_active   = false;
        m_pso                  = RHI_PipelineState();
        m_pipeline_state_dirty = false;
        m_scissor_valid        = false;
        m_viewport_valid       = false;
        m_vrs_valid            = false;

        Profiler::SubmissionBegin(this, m_queue->GetType());
    }

    void RHI_CommandList::Submit(RHI_SyncPrimitive* semaphore_wait, const bool is_immediate, RHI_SyncPrimitive* semaphore_signal,
                                 RHI_SyncPrimitive* semaphore_timeline_wait, uint64_t timeline_wait_value)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        CommitPendingClear();
        render_pass_end();

        State& state = get_state(m_rhi_state);
        end_render(state);
        end_compute(state);

        Profiler::SubmissionEnd(this);

        WGPUCommandBufferDescriptor descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
        descriptor.label                       = webgpu::label(m_object_name.c_str());
        WGPUCommandBuffer command_buffer       = wgpuCommandEncoderFinish(state.encoder, &descriptor);
        wgpuCommandEncoderRelease(state.encoder);
        state.encoder  = nullptr;
        m_rhi_resource = nullptr;

        {
            // cpu shadows and the upload ring go over first, the queue orders the writes before the commands
            lock_guard<mutex> lock(mutex_submit);
            for (RHI_Buffer* buffer : state.touched)
            {
                webgpu::buffer_flush(buffer, true);
            }
            for (const webgpu::BindlessBuffer& bindless : webgpu::bindless_buffers())
            {
                if (bindless.buffer)
                {
                    webgpu::buffer_flush(bindless.buffer, true);
                }
            }
            flush_chunks(state);

            m_last_timeline_signal_value = m_queue->Submit(command_buffer, 0, semaphore_wait, semaphore_signal, m_rendering_complete_semaphore_timeline.get(), semaphore_timeline_wait, timeline_wait_value, &m_submission_order);
        }
        wgpuCommandBufferRelease(command_buffer);
        state.touched.clear();
        for (RHI_Buffer* buffer : state.readbacks)
        {
            webgpu::buffer_read_back(buffer);
        }
        state.readbacks.clear();
        CommitTrackedResources();

        if (semaphore_wait)
        {
            semaphore_wait->SetConsumer(GetWork());
        }

        m_state = RHI_CommandListState::Submitted;

        if (is_immediate)
        {
            WaitForExecution();
        }
    }

    // everything the cpu hands the gpu goes through queue writes, so a submitted list can be recorded again right away
    void RHI_CommandList::WaitForExecution(const bool log_wait_time)
    {
        if (m_state != RHI_CommandListState::Submitted)
            return;

        webgpu::process_events();
        m_state = RHI_CommandListState::Idle;
    }

    bool RHI_CommandList::IsExecutionComplete()
    {
        return m_state != RHI_CommandListState::Recording;
    }

    void RHI_CommandList::set_pipeline_state(RHI_PipelineState& pso)
    {
        pso.Prepare();
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_pipeline_state_dirty && m_pso.GetHash() == pso.GetHash() && m_pso.use_standard_resources == pso.use_standard_resources)
            return;

        ResetTrackedBindings();

        // a matching vertex shader and array index means the same attachments continue, so load instead of clear
        const bool same_pass = m_pso.shaders[RHI_Shader_Type::Vertex] != nullptr && m_pso.shaders[RHI_Shader_Type::Vertex] == pso.shaders[RHI_Shader_Type::Vertex] && m_pso.render_target_array_index == pso.render_target_array_index;
        m_load_depth_render_target = same_pass && pso.render_target_depth_texture == m_pso.render_target_depth_texture;
        for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
        {
            m_load_color_render_targets[i] = same_pass && pso.render_target_color_textures[i] == m_pso.render_target_color_textures[i];
        }

        RHI_Pipeline* pipeline                         = nullptr;
        RHI_DescriptorSetLayout* descriptor_set_layout = nullptr;
        RHI_Device::GetOrCreatePipeline(pso, pipeline, descriptor_set_layout);

        // the browser finishes pipelines between frames, until then this one isn't there
        webgpu::Pipeline* resource = ready_pipeline(pipeline);
        if (!resource)
        {
            pipeline = nullptr;
        }

        m_pso         = pso;
        m_pso_pending = pso;
        m_pipeline    = pipeline;
        m_cull_mode   = pso.cull_mode;

        State& state         = get_state(m_rhi_state);
        state.pipeline         = resource;
        state.pipeline_variant = nullptr;
        state.resources.fill({});
        state.resources_dirty = true;
        state.push_dirty      = true;
        if (pso.IsCompute() || pso.IsRayTracing())
        {
            render_pass_end();
        }
        else
        {
            RenderPassBegin();
        }

        if (pipeline && pso.use_standard_resources)
        {
            RHI_Device::InvokePipelineBound(this);
        }

        m_push_constant_size   = 0;
        m_pipeline_state_dirty = !pipeline;
    }

    void RHI_CommandList::RenderPassBegin()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        render_pass_end();
        if (!m_pso.IsGraphics())
            return;

        SynchronizeRenderTargets();

        State& state = get_state(m_rhi_state);
        end_compute(state);

        array<WGPURenderPassColorAttachment, rhi_max_render_target_count> colors = {};
        uint32_t color_count = 0;
        uint32_t width       = 0;
        uint32_t height      = 0;
        auto clear_value     = [](const Color& color) { return WGPUColor{ color.r, color.g, color.b, color.a }; };

        if (RHI_SwapChain* swapchain = m_pso.render_target_swapchain)
        {
            if (!swapchain->GetRhiRtv())
                return;

            const bool clear = m_pso.clear_color[0] != rhi_color_load && m_pso.clear_color[0] != rhi_color_dont_care;
            colors[0]        = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colors[0].view   = static_cast<WGPUTextureView>(swapchain->GetRhiRtv());
            colors[0].loadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
            colors[0].storeOp = WGPUStoreOp_Store;
            if (clear)
            {
                colors[0].clearValue = clear_value(m_pso.clear_color[0]);
            }
            color_count = 1;
            width       = swapchain->GetWidth();
            height      = swapchain->GetHeight();
        }
        else
        {
            for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
            {
                colors[i] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                RHI_Texture* texture = m_pso.render_target_color_textures[i];
                if (!texture || !texture->GetRhiResource())
                    continue;

                const bool is_3d = texture->GetType() == RHI_Texture_Type::Type3D;
                colors[i].view   = webgpu::texture_view(texture, is_3d ? WGPUTextureViewDimension_3D : WGPUTextureViewDimension_2D, 0, 1, is_3d ? 0 : m_pso.render_target_array_index, 1, false);
                if (is_3d)
                {
                    colors[i].depthSlice = m_pso.render_target_array_index;
                }

                const bool clear  = !m_load_color_render_targets[i] && m_pso.clear_color[i] != rhi_color_load && m_pso.clear_color[i] != rhi_color_dont_care;
                colors[i].loadOp  = clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
                colors[i].storeOp = WGPUStoreOp_Store;
                if (clear)
                {
                    colors[i].clearValue = clear_value(m_pso.clear_color[i]);
                }
                color_count            = i + 1;
                state.attachments[i]   = texture;
                width                  = texture->GetResidentWidth();
                height                 = texture->GetResidentHeight();
            }
        }

        WGPURenderPassDepthStencilAttachment depth_attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        bool has_depth                                        = false;
        if (RHI_Texture* depth = m_pso.render_target_depth_texture)
        {
            if (depth->GetRhiResource())
            {
                const bool clear_depth         = !m_load_depth_render_target && m_pso.clear_depth != rhi_depth_load && m_pso.clear_depth != rhi_depth_dont_care;
                depth_attachment.view          = webgpu::texture_view(depth, WGPUTextureViewDimension_2D, 0, 1, m_pso.render_target_array_index, 1, false);
                depth_attachment.depthLoadOp   = clear_depth ? WGPULoadOp_Clear : WGPULoadOp_Load;
                depth_attachment.depthStoreOp  = WGPUStoreOp_Store;
                depth_attachment.depthClearValue = clear_depth ? m_pso.clear_depth : 1.0f;
                if (depth->IsStencilFormat())
                {
                    const bool clear_stencil           = !m_load_depth_render_target && m_pso.clear_stencil != rhi_stencil_load && m_pso.clear_stencil != rhi_stencil_dont_care;
                    depth_attachment.stencilLoadOp     = clear_stencil ? WGPULoadOp_Clear : WGPULoadOp_Load;
                    depth_attachment.stencilStoreOp    = WGPUStoreOp_Store;
                    depth_attachment.stencilClearValue = clear_stencil ? m_pso.clear_stencil : 0;
                }
                has_depth                                    = true;
                state.attachments[rhi_max_render_target_count] = depth;

                if (width == 0)
                {
                    width  = depth->GetResidentWidth();
                    height = depth->GetResidentHeight();
                }
            }
        }

        // a pass needs at least one attachment
        if (color_count == 0 && !has_depth)
            return;

        WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        descriptor.label                    = webgpu::label(m_pso.name);
        descriptor.colorAttachmentCount     = color_count;
        descriptor.colorAttachments         = colors.data();
        descriptor.depthStencilAttachment   = has_depth ? &depth_attachment : nullptr;
        state.render                        = wgpuCommandEncoderBeginRenderPass(state.encoder, &descriptor);
        state.pass_width                    = width;
        state.pass_height                   = height;
        state.pipeline_bound                = nullptr;
        state.resources_dirty               = true;
        state.push_dirty                    = true;

        m_viewport_valid = false;
        m_scissor_valid  = false;
        if (width > 0 && height > 0)
        {
            RHI_Viewport viewport;
            viewport.width  = static_cast<float>(width);
            viewport.height = static_cast<float>(height);
            set_viewport(viewport);
            set_scissor_rectangle(math::Rectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)));
        }

        m_render_pass_active = true;
    }

    void RHI_CommandList::render_pass_end()
    {
        if (!m_render_pass_active)
            return;

        m_render_pass_active = false;
        end_render(get_state(m_rhi_state));

        if (m_pso.render_target_swapchain)
            return;

        for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
        {
            if (RHI_Texture* texture = m_pso.render_target_color_textures[i])
            {
                SetTrackedTextureLayout(texture, 0, texture->GetResidentMipCount(), RHI_Image_Layout::General);
            }
        }

        if (RHI_Texture* depth = m_pso.render_target_depth_texture)
        {
            SetTrackedTextureLayout(depth, 0, depth->GetResidentMipCount(), RHI_Image_Layout::General);
        }
    }

    void RHI_CommandList::clear_texture(RHI_Texture* texture, const Color& clear_color, const float clear_depth, const uint32_t clear_stencil)
    {
        if (!texture || !texture->GetRhiResource())
            return;

        if (!texture->IsDepthStencilFormat() && (clear_color == rhi_color_load || clear_color == rhi_color_dont_care))
            return;

        WGPUTexture resource = static_cast<WGPUTexture>(texture->GetRhiResource());
        if (!(wgpuTextureGetUsage(resource) & WGPUTextureUsage_RenderAttachment))
        {
            static once_flag logged;
            call_once(logged, [texture]()
            {
                SP_LOG_WARNING("%s can't be a render target, webgpu clears through render passes", texture->GetObjectName().c_str());
            });
            return;
        }

        render_pass_end();
        State& state = get_state(m_rhi_state);
        end_compute(state);

        const RHI_Barrier_Scope scope = texture->IsUav() ? RHI_Barrier_Scope::Compute : RHI_Barrier_Scope::Graphics;
        PrepareForExternalWrite(texture, RHI_Image_Layout::General, scope);

        const bool is_3d      = texture->GetType() == RHI_Texture_Type::Type3D;
        const uint32_t layers = is_3d ? max(1u, texture->GetDepth()) : max(1u, texture->GetArrayLength());
        for (uint32_t layer = 0; layer < layers; layer++)
        {
            WGPURenderPassDescriptor descriptor                   = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            WGPURenderPassColorAttachment color                   = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            WGPURenderPassDepthStencilAttachment depth_attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            descriptor.label                                      = webgpu::label("clear");
            if (texture->IsDepthStencilFormat())
            {
                depth_attachment.view            = webgpu::texture_view(texture, WGPUTextureViewDimension_2D, 0, 1, layer, 1, false);
                depth_attachment.depthLoadOp     = WGPULoadOp_Clear;
                depth_attachment.depthStoreOp    = WGPUStoreOp_Store;
                depth_attachment.depthClearValue = (clear_depth == rhi_depth_load || clear_depth == rhi_depth_dont_care) ? 1.0f : clear_depth;
                if (texture->IsStencilFormat())
                {
                    depth_attachment.stencilLoadOp     = WGPULoadOp_Clear;
                    depth_attachment.stencilStoreOp    = WGPUStoreOp_Store;
                    depth_attachment.stencilClearValue = (clear_stencil == rhi_stencil_load || clear_stencil == rhi_stencil_dont_care) ? 0 : clear_stencil;
                }
                descriptor.depthStencilAttachment = &depth_attachment;
            }
            else
            {
                color.view       = webgpu::texture_view(texture, is_3d ? WGPUTextureViewDimension_3D : WGPUTextureViewDimension_2D, 0, 1, is_3d ? 0 : layer, 1, false);
                color.depthSlice = is_3d ? layer : WGPU_DEPTH_SLICE_UNDEFINED;
                color.loadOp     = WGPULoadOp_Clear;
                color.storeOp    = WGPUStoreOp_Store;
                color.clearValue = { clear_color.r, clear_color.g, clear_color.b, clear_color.a };
                descriptor.colorAttachmentCount = 1;
                descriptor.colorAttachments     = &color;
            }

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(state.encoder, &descriptor);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }

        SetTrackedTextureLayout(texture, 0, texture->GetResidentMipCount(), RHI_Image_Layout::General);
        TrackExternalTextureUsage(texture, RHI_Resource_Access::Write, RHI_Image_Layout::General, scope, RHI_Resource_Usage::Attachment);
    }

    // binds the pipeline and the groups of the next draw or dispatch, m_pipeline is null when there is nothing to run
    void RHI_CommandList::PreDraw()
    {
        PrepareDispatch();
        if (!m_pipeline)
            return;

        State& state               = get_state(m_rhi_state);
        webgpu::Pipeline* pipeline = state.pipeline;
        if (!pipeline)
        {
            m_pipeline = nullptr;
            return;
        }

        const bool compute = m_pso.IsCompute();
        if (compute)
        {
            if (!pipeline->compute)
            {
                m_pipeline = nullptr;
                return;
            }

            render_pass_end();
            if (!state.compute)
            {
                WGPUComputePassDescriptor descriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
                state.compute                        = wgpuCommandEncoderBeginComputePass(state.encoder, &descriptor);
                state.pipeline_bound                 = nullptr;
            }
        }
        else
        {
            if (!pipeline->render)
            {
                m_pipeline = nullptr;
                return;
            }

            // a copy or clear in the middle of a pass ended it, the pass continues in a new one that loads
            if (!state.render)
            {
                const RHI_Viewport viewport(m_viewport_x, m_viewport_y, m_viewport_width, m_viewport_height, m_viewport_depth_min, m_viewport_depth_max);
                const math::Rectangle scissor(m_scissor_x, m_scissor_y, m_scissor_width, m_scissor_height);
                m_load_depth_render_target = true;
                m_load_color_render_targets.fill(true);
                RenderPassBegin();
                if (!state.render)
                {
                    m_pipeline = nullptr;
                    return;
                }
                set_viewport(viewport);
                set_scissor_rectangle(scissor);
            }
        }

        SynchronizeResources();
        FlushBarriers();

        if (state.resources_dirty || !state.pipeline_variant)
        {
            state.pipeline_variant = select_variant(state, pipeline);
        }
        pipeline = state.pipeline_variant;

        if (state.pipeline_bound != pipeline)
        {
            if (compute)
            {
                wgpuComputePassEncoderSetPipeline(state.compute, pipeline->compute);
            }
            else
            {
                wgpuRenderPassEncoderSetPipeline(state.render, pipeline->render);
            }
            state.pipeline_bound  = pipeline;
            state.resources_dirty = true;
            state.push_dirty      = true;
        }

        auto set_group = [&](const uint32_t index, WGPUBindGroup group, const uint32_t offset_count, const uint32_t* offsets)
        {
            if (compute)
            {
                wgpuComputePassEncoderSetBindGroup(state.compute, index, group, offset_count, offsets);
            }
            else
            {
                wgpuRenderPassEncoderSetBindGroup(state.render, index, group, offset_count, offsets);
            }
        };

        if (state.resources_dirty)
        {
            WGPUBindGroup group = create_pass_group(state, pipeline);
            set_group(webgpu::group_pass, group, 0, nullptr);
            wgpuBindGroupRelease(group);
            set_group(webgpu::group_bindless, get_bindless_group(pipeline), 0, nullptr);
            state.resources_dirty = false;
        }

        if (state.push_dirty)
        {
            uint32_t chunk  = 0;
            uint64_t offset = 0;
            uint8_t* data   = allocate(state, webgpu::push_constants_size, webgpu::push_constants_size, chunk, offset);
            memcpy(data, state.push_data.data(), webgpu::push_constants_size);
            state.push_chunk  = chunk;
            state.push_offset = static_cast<uint32_t>(offset);
            state.push_dirty  = false;
        }
        set_group(webgpu::group_push, state.chunks[state.push_chunk].push_group, 1, &state.push_offset);

        if (!compute)
        {
            if (pipeline->uses_vertex_buffer)
            {
                WGPUBuffer buffer = state.vertex_buffer ? state.vertex_buffer : webgpu::zero_vertex_buffer();
                wgpuRenderPassEncoderSetVertexBuffer(state.render, 0, buffer, 0, state.vertex_buffer ? state.vertex_size : WGPU_WHOLE_SIZE);
            }

            if (pipeline->uses_zero_buffer)
            {
                wgpuRenderPassEncoderSetVertexBuffer(state.render, pipeline->zero_buffer_slot, webgpu::zero_vertex_buffer(), 0, WGPU_WHOLE_SIZE);
            }

            if (state.index_buffer)
            {
                wgpuRenderPassEncoderSetIndexBuffer(state.render, state.index_buffer, state.index_format, 0, state.index_size);
            }
        }
    }

    void RHI_CommandList::draw(const uint32_t vertex_count, uint32_t vertex_start_index)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        PreDraw();
        if (!m_pipeline)
            return;

        wgpuRenderPassEncoderDraw(get_state(m_rhi_state).render, vertex_count, 1, vertex_start_index, 0);
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::draw_indexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset, const uint32_t instance_start_index, const uint32_t instance_count)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        PreDraw();
        State& state = get_state(m_rhi_state);
        if (!m_pipeline)
            return;

        if (!state.index_buffer)
            return;

        wgpuRenderPassEncoderDrawIndexed(state.render, index_count, instance_count, index_offset, static_cast<int32_t>(vertex_offset), instance_start_index);
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::draw_indexed_indirect(RHI_Buffer* args_buffer, const uint32_t args_offset, const uint32_t draw_count)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(args_buffer != nullptr && draw_count != 0);
        TrackBufferRead(3, args_buffer, RHI_Resource_Usage::Indirect);
        PreDraw();
        if (!m_pipeline)
            return;

        State& state = get_state(m_rhi_state);
        if (!state.index_buffer)
            return;

        // one indirect command at a time, the layout matches d3d12's draw indexed arguments
        state.touched.insert(args_buffer);
        WGPUBuffer arguments  = static_cast<WGPUBuffer>(args_buffer->GetRhiResource());
        const uint64_t stride = 5 * sizeof(uint32_t);
        for (uint32_t i = 0; i < draw_count; i++)
        {
            wgpuRenderPassEncoderDrawIndexedIndirect(state.render, arguments, args_offset + i * stride);
        }
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::draw_indexed_indirect_count(RHI_Buffer* args_buffer, const uint32_t args_offset, RHI_Buffer* count_buffer, const uint32_t count_offset, const uint32_t max_draw_count)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        TrackBufferRead(3, args_buffer, RHI_Resource_Usage::Indirect);
        TrackBufferRead(4, count_buffer, RHI_Resource_Usage::Indirect);

        static once_flag logged;
        call_once(logged, []()
        {
            SP_LOG_WARNING("Indirect draws with a gpu count aren't supported by the webgpu backend yet");
        });
    }

    void RHI_CommandList::dispatch(uint32_t x, uint32_t y, uint32_t z)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        PreDraw();
        if (!m_pipeline || x == 0 || y == 0 || z == 0)
            return;

        wgpuComputePassEncoderDispatchWorkgroups(get_state(m_rhi_state).compute, x, y, z);
    }

    void RHI_CommandList::draw_indirect(RHI_Buffer* args_buffer, const uint32_t args_offset)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(args_buffer != nullptr);
        TrackBufferRead(3, args_buffer, RHI_Resource_Usage::Indirect);
        PreDraw();
        if (!m_pipeline)
            return;

        State& state = get_state(m_rhi_state);
        state.touched.insert(args_buffer);
        wgpuRenderPassEncoderDrawIndirect(state.render, static_cast<WGPUBuffer>(args_buffer->GetRhiResource()), args_offset);
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::draw_mesh_tasks_indirect(RHI_Buffer* args_buffer, const uint32_t args_offset)
    {
        // webgpu has no mesh shaders, the device reports them as unsupported
    }

    void RHI_CommandList::dispatch_indirect(RHI_Buffer* args_buffer, const uint32_t args_offset)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(args_buffer != nullptr);
        TrackBufferRead(3, args_buffer, RHI_Resource_Usage::Indirect);
        PreDraw();
        if (!m_pipeline)
            return;

        State& state = get_state(m_rhi_state);
        state.touched.insert(args_buffer);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(state.compute, static_cast<WGPUBuffer>(args_buffer->GetRhiResource()), args_offset);
    }

    void RHI_CommandList::trace_rays(const uint32_t width, const uint32_t height)
    {
        // webgpu has no ray tracing, the device reports it as unsupported
    }

    void RHI_CommandList::set_acceleration_structure(const uint32_t slot, RHI_AccelerationStructure* tlas)
    {
    }

    void RHI_CommandList::blit(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips, const float resolution_scale)
    {
        SP_ASSERT(source && destination);
        render_pass_end();

        WGPUTexture texture_source      = static_cast<WGPUTexture>(source->GetRhiResource());
        WGPUTexture texture_destination = static_cast<WGPUTexture>(destination->GetRhiResource());
        if (!texture_source || !texture_destination)
            return;

        State& state = get_state(m_rhi_state);
        end_compute(state);

        PrepareForExternalRead(source, RHI_Image_Layout::General, RHI_Barrier_Scope::Graphics);
        PrepareForExternalWrite(destination, RHI_Image_Layout::General, RHI_Barrier_Scope::Graphics);

        const WGPUTextureFormat format_source      = wgpuTextureGetFormat(texture_source);
        const WGPUTextureFormat format_destination = wgpuTextureGetFormat(texture_destination);
        const bool same_size   = source->GetResidentWidth() == destination->GetResidentWidth() && source->GetResidentHeight() == destination->GetResidentHeight();
        const bool same_format = format_source == format_destination;
        if (same_size && same_format)
        {
            const uint32_t mip_count   = blit_mips ? min(source->GetResidentMipCount(), destination->GetResidentMipCount()) : 1;
            const uint32_t layer_count = source->GetType() == RHI_Texture_Type::Type3D ? 1 : min(source->GetArrayLength(), destination->GetArrayLength());
            for (uint32_t mip = 0; mip < mip_count; mip++)
            {
                WGPUTexelCopyTextureInfo copy_source      = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                WGPUTexelCopyTextureInfo copy_destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                copy_source.texture                       = texture_source;
                copy_source.mipLevel                      = mip;
                copy_destination.texture                  = texture_destination;
                copy_destination.mipLevel                 = mip;
                const uint32_t depth                      = source->GetType() == RHI_Texture_Type::Type3D ? max(1u, source->GetDepth() >> mip) : layer_count;
                const WGPUExtent3D extent                 = { max(1u, source->GetResidentWidth() >> mip), max(1u, source->GetResidentHeight() >> mip), depth };
                wgpuCommandEncoderCopyTextureToTexture(state.encoder, &copy_source, &copy_destination, &extent);
            }
        }
        else if (wgpuTextureGetUsage(texture_destination) & WGPUTextureUsage_RenderAttachment)
        {
            WGPUTextureView view_source      = webgpu::texture_view(source, WGPUTextureViewDimension_2D, 0, 1, 0, 1, false);
            WGPUTextureView view_destination = webgpu::texture_view(destination, WGPUTextureViewDimension_2D, 0, 1, 0, 1, false);
            webgpu_blit::draw(state, view_source, format_source, view_destination, format_destination);
        }

        SetTrackedTextureLayout(source, 0, source->GetResidentMipCount(), RHI_Image_Layout::General);
        SetTrackedTextureLayout(destination, 0, destination->GetResidentMipCount(), RHI_Image_Layout::General);
    }

    void RHI_CommandList::blit(RHI_Texture* source, RHI_SwapChain* destination)
    {
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
        render_pass_end();

        WGPUTexture texture_source = static_cast<WGPUTexture>(source->GetRhiResource());
        if (!texture_source || !destination->GetRhiRtv())
            return;

        State& state = get_state(m_rhi_state);
        end_compute(state);

        PrepareForExternalRead(source, RHI_Image_Layout::General, RHI_Barrier_Scope::Graphics);
        WGPUTextureView view_source = webgpu::texture_view(source, WGPUTextureViewDimension_2D, 0, 1, 0, 1, false);
        webgpu_blit::draw(state, view_source, wgpuTextureGetFormat(texture_source), static_cast<WGPUTextureView>(destination->GetRhiRtv()), webgpu::surface_format());
        SetTrackedTextureLayout(source, 0, source->GetResidentMipCount(), RHI_Image_Layout::General);
    }

    void RHI_CommandList::blit_to_array_layer(RHI_Texture* source, RHI_Texture* destination, uint32_t dst_layer)
    {
        SP_ASSERT(source && destination);
        render_pass_end();

        WGPUTexture texture_source      = static_cast<WGPUTexture>(source->GetRhiResource());
        WGPUTexture texture_destination = static_cast<WGPUTexture>(destination->GetRhiResource());
        if (!texture_source || !texture_destination || wgpuTextureGetFormat(texture_source) != wgpuTextureGetFormat(texture_destination))
            return;

        State& state = get_state(m_rhi_state);
        end_compute(state);

        PrepareForExternalRead(source, RHI_Image_Layout::General, RHI_Barrier_Scope::Graphics);
        PrepareForExternalWrite(destination, RHI_Image_Layout::General, RHI_Barrier_Scope::Graphics);
        const uint32_t mip_count = min(source->GetResidentMipCount(), destination->GetResidentMipCount());
        for (uint32_t mip = 0; mip < mip_count; mip++)
        {
            WGPUTexelCopyTextureInfo copy_source      = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            WGPUTexelCopyTextureInfo copy_destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            copy_source.texture                       = texture_source;
            copy_source.mipLevel                      = mip;
            copy_destination.texture                  = texture_destination;
            copy_destination.mipLevel                 = mip;
            copy_destination.origin                   = { 0, 0, dst_layer };
            const WGPUExtent3D extent                 = { max(1u, source->GetResidentWidth() >> mip), max(1u, source->GetResidentHeight() >> mip), 1 };
            wgpuCommandEncoderCopyTextureToTexture(state.encoder, &copy_source, &copy_destination, &extent);
        }
    }

    void RHI_CommandList::blit_to_xr_swapchain(RHI_Texture* source)
    {
        // xr is unavailable on webgpu
    }

    void RHI_CommandList::copy(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips)
    {
        blit(source, destination, blit_mips, 1.0f);
    }

    void RHI_CommandList::copy(RHI_Texture* source, RHI_SwapChain* destination)
    {
        // the surface can be bgra while engine textures are rgba, so even a copy goes through the blit
        blit(source, destination);
    }

    void RHI_CommandList::set_viewport(const RHI_Viewport& viewport) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        m_viewport_x         = viewport.x;
        m_viewport_y         = viewport.y;
        m_viewport_width     = viewport.width;
        m_viewport_height    = viewport.height;
        m_viewport_depth_min = viewport.depth_min;
        m_viewport_depth_max = viewport.depth_max;
        m_viewport_valid     = true;

        // webgpu requires the viewport inside the attachments
        State& state = get_state(m_rhi_state);
        if (state.render)
        {
            const float width     = static_cast<float>(state.pass_width);
            const float height    = static_cast<float>(state.pass_height);
            const float x         = clamp(viewport.x, 0.0f, width);
            const float y         = clamp(viewport.y, 0.0f, height);
            const float w         = clamp(viewport.width, 0.0f, width - x);
            const float h         = clamp(viewport.height, 0.0f, height - y);
            const float depth_min = clamp(viewport.depth_min, 0.0f, 1.0f);
            const float depth_max = clamp(viewport.depth_max, depth_min, 1.0f);
            wgpuRenderPassEncoderSetViewport(state.render, x, y, w, h, depth_min, depth_max);
        }
    }

    void RHI_CommandList::set_scissor_rectangle(const math::Rectangle& scissor_rectangle) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        m_scissor_x      = scissor_rectangle.x;
        m_scissor_y      = scissor_rectangle.y;
        m_scissor_width  = scissor_rectangle.width;
        m_scissor_height = scissor_rectangle.height;
        m_scissor_valid  = true;

        // d3d12 and vulkan clip a scissor to the attachments, webgpu requires it to fit
        State& state = get_state(m_rhi_state);
        if (state.render)
        {
            const float width  = static_cast<float>(state.pass_width);
            const float height = static_cast<float>(state.pass_height);
            const float left   = clamp(scissor_rectangle.x, 0.0f, width);
            const float top    = clamp(scissor_rectangle.y, 0.0f, height);
            const float right  = clamp(scissor_rectangle.x + scissor_rectangle.width, left, width);
            const float bottom = clamp(scissor_rectangle.y + scissor_rectangle.height, top, height);
            wgpuRenderPassEncoderSetScissorRect(state.render, static_cast<uint32_t>(left), static_cast<uint32_t>(top), static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top));
        }
    }

    // the cull mode is part of the webgpu pipeline, a change binds the variant that has it
    void RHI_CommandList::set_cull_mode(const RHI_CullMode cull_mode)
    {
        m_cull_mode = cull_mode;
        if (!m_pipeline || m_pso.cull_mode == cull_mode || !m_pso.IsGraphics())
            return;

        RHI_PipelineState pso = m_pso;
        pso.cull_mode         = cull_mode;
        pso.Prepare();

        RHI_Pipeline* pipeline                         = nullptr;
        RHI_DescriptorSetLayout* descriptor_set_layout = nullptr;
        RHI_Device::GetOrCreatePipeline(pso, pipeline, descriptor_set_layout);
        webgpu::Pipeline* resource = ready_pipeline(pipeline);
        if (!resource)
            return;

        m_pso.cull_mode = cull_mode;
        m_pso.Prepare();
        m_pipeline                       = pipeline;
        get_state(m_rhi_state).pipeline  = resource;
    }

    void RHI_CommandList::set_buffer_vertex(const RHI_Buffer* vertex, RHI_Buffer* instance)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(vertex && vertex->GetRhiResource());
        TrackBufferRead(0, const_cast<RHI_Buffer*>(vertex), RHI_Resource_Usage::Vertex);
        TrackBufferRead(1, instance, RHI_Resource_Usage::Vertex);

        State& state        = get_state(m_rhi_state);
        state.vertex_buffer = static_cast<WGPUBuffer>(vertex->GetRhiResource());
        state.vertex_size   = wgpuBufferGetSize(state.vertex_buffer);
        state.touched.insert(const_cast<RHI_Buffer*>(vertex));
        m_buffer_id_vertex   = vertex->GetObjectId();
        m_buffer_id_instance = instance ? instance->GetObjectId() : 0;
        if (state.render && state.pipeline_bound && state.pipeline_bound->uses_vertex_buffer)
        {
            wgpuRenderPassEncoderSetVertexBuffer(state.render, 0, state.vertex_buffer, 0, state.vertex_size);
        }
        Profiler::m_rhi_bindings_buffer_vertex++;
    }

    void RHI_CommandList::set_buffer_index(const RHI_Buffer* buffer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(buffer && buffer->GetRhiResource());
        TrackBufferRead(2, const_cast<RHI_Buffer*>(buffer), RHI_Resource_Usage::Index);

        State& state       = get_state(m_rhi_state);
        state.index_buffer = static_cast<WGPUBuffer>(buffer->GetRhiResource());
        state.index_size   = wgpuBufferGetSize(state.index_buffer);
        state.index_format = buffer->GetStride() == sizeof(uint16_t) ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
        state.touched.insert(const_cast<RHI_Buffer*>(buffer));
        m_buffer_id_index  = buffer->GetObjectId();
        if (state.render)
        {
            wgpuRenderPassEncoderSetIndexBuffer(state.render, state.index_buffer, state.index_format, 0, state.index_size);
        }
        Profiler::m_rhi_bindings_buffer_index++;
    }

    void RHI_CommandList::set_constant_buffer(const uint32_t slot, RHI_Buffer* constant_buffer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        if (!constant_buffer || !constant_buffer->GetRhiResource())
            return;

        State& state       = get_state(m_rhi_state);
        Resource& resource = state.resources[rhi_shader_register_shift_b + slot];
        resource.type      = Resource::Type::ConstantBuffer;
        resource.buffer    = constant_buffer;
        resource.offset    = constant_buffer->GetOffset();
        resource.size      = constant_buffer->GetStride();
        state.touched.insert(constant_buffer);
        state.resources_dirty   = true;
        m_constant_buffer_bound = true;
    }

    void RHI_CommandList::push_constants(const uint32_t offset, const uint32_t size, const void* data)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        TryBindPendingPipeline();
        if (!m_pipeline)
            return;

        SP_ASSERT(data != nullptr && offset + size <= webgpu::push_constants_size);
        State& state = get_state(m_rhi_state);
        memcpy(state.push_data.data() + offset, data, size);
        state.push_dirty     = true;
        m_push_constant_size = size;
    }

    void RHI_CommandList::set_buffer(const uint32_t slot, RHI_Buffer* buffer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        RHI_Resource_Access access = GetBufferAccess(slot);
        if (buffer && buffer->GetRhiResource() && access == RHI_Resource_Access::None)
        {
            access = RHI_Resource_Access::Read;
        }
        TrackBufferUsage(slot, buffer && buffer->GetRhiResource() ? buffer : nullptr, access);
        if (!buffer || !buffer->GetRhiResource() || access == RHI_Resource_Access::None || slot >= rhi_max_resource_slots)
            return;

        // reads go to the t registers, writes to the u registers
        State& state       = get_state(m_rhi_state);
        const bool read    = access == RHI_Resource_Access::Read;
        Resource& resource = state.resources[(read ? rhi_shader_register_shift_t : rhi_shader_register_shift_u) + slot];
        resource.type      = Resource::Type::Buffer;
        resource.buffer    = buffer;
        resource.offset    = 0;
        resource.size      = 0;
        state.touched.insert(buffer);
        state.resources_dirty = true;
    }

    void RHI_CommandList::set_texture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index, uint32_t mip_range, const bool uav, const uint32_t array_layer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!IsTextureBindingUsed(slot, uav))
            return;

        TrackTextureUsage(slot, texture, mip_index, mip_range, array_layer, uav);
        if (slot >= rhi_max_resource_slots)
            return;

        State& state          = get_state(m_rhi_state);
        const uint32_t base   = uav ? rhi_shader_register_shift_u : rhi_shader_register_shift_t;
        state.resources_dirty = true;
        if (!texture || !texture->GetRhiResource())
        {
            state.resources[base + slot] = {};
            return;
        }

        // a uav sees one mip, a mip range of uavs (spd) is an array of views over consecutive u registers
        const bool mip_specified = mip_index != rhi_all_mips;
        const uint32_t mip_first = mip_specified ? mip_index : 0;
        const uint32_t mip_count = mip_specified ? max(mip_range, 1u) : (uav ? 1 : texture->GetResidentMipCount());
        const uint32_t views     = uav ? mip_count : 1;
        for (uint32_t i = 0; i < views && slot + i < rhi_max_resource_slots; i++)
        {
            Resource& resource = state.resources[base + slot + i];
            resource.type      = Resource::Type::Texture;
            resource.texture   = texture;
            resource.mip       = mip_first + i;
            resource.mip_count = uav ? 1 : mip_count;
            resource.layer     = array_layer;
        }
    }

    uint32_t RHI_CommandList::begin_timestamp()
    {
        // timestamp queries need an optional feature, the profiler shows cpu time only
        return UINT32_MAX;
    }

    uint32_t RHI_CommandList::end_timestamp()
    {
        return UINT32_MAX;
    }

    void RHI_CommandList::ReadbackTimestampsForProfiler()
    {
        if (!m_timestamp_sample || m_timestamp_sample->ready)
            return;

        m_timestamp_sample->count = 0;
        m_timestamp_sample->ready = true;
    }

    void RHI_CommandList::begin_occlusion_query(const uint64_t entity_id)
    {
    }

    void RHI_CommandList::end_occlusion_query()
    {
    }

    bool RHI_CommandList::GetOcclusionQueryResult(const uint64_t entity_id)
    {
        // visible, so nothing gets culled by a query that never ran
        return true;
    }

    void RHI_CommandList::update_occlusion_queries()
    {
    }

    void RHI_CommandList::begin_timeblock(const char* name, const bool gpu_marker, const bool gpu_timing)
    {
        SP_ASSERT(name != nullptr);

        RHI_Queue_Type queue_type = m_queue ? m_queue->GetType() : RHI_Queue_Type::Max;
        Profiler::TimeBlockStart(name, TimeBlockType::Cpu, this, queue_type);
        m_timeblock_gpu_timing.push(false);
    }

    void RHI_CommandList::end_timeblock()
    {
        if (!m_timeblock_gpu_timing.empty())
        {
            m_timeblock_gpu_timing.pop();
        }

        Profiler::TimeBlockEnd(TimeBlockType::Cpu, this);
    }

    // debug groups have to balance per pass encoder, which engine markers don't respect
    void RHI_CommandList::begin_marker(const char* name)
    {
    }

    void RHI_CommandList::end_marker()
    {
    }

    void RHI_CommandList::write_gpu_breadcrumb(RHI_Buffer* buffer, uint32_t slot, uint32_t value)
    {
    }

    void RHI_CommandList::update_buffer(RHI_Buffer* buffer, const uint64_t offset, const uint64_t size, const void* data, const bool use_mapped_memory)
    {
        if (!buffer || !data || size == 0)
            return;

        if (use_mapped_memory && buffer->GetMappedData())
        {
            memcpy(static_cast<uint8_t*>(buffer->GetMappedData()) + offset, data, static_cast<size_t>(size));
            webgpu::buffer_mark_dirty(buffer, offset, size);
            get_state(m_rhi_state).touched.insert(buffer);
            return;
        }

        WGPUBuffer destination = static_cast<WGPUBuffer>(buffer->GetRhiResource());
        if (!destination)
            return;

        // copies need 4 byte offsets and sizes, anything else goes straight to the queue
        if ((offset & 3) || (size & 3))
        {
            wgpuQueueWriteBuffer(webgpu::queue(), destination, offset, data, static_cast<size_t>(size));
            return;
        }

        render_pass_end();
        State& state = get_state(m_rhi_state);
        end_compute(state);

        uint32_t chunk          = 0;
        uint64_t chunk_offset   = 0;
        uint8_t* staging        = allocate(state, size, 4, chunk, chunk_offset);
        memcpy(staging, data, static_cast<size_t>(size));
        wgpuCommandEncoderCopyBufferToBuffer(state.encoder, state.chunks[chunk].buffer, chunk_offset, destination, offset, size);
    }

    void RHI_CommandList::InsertBarrier(const RHI_Barrier& barrier)
    {
        // webgpu tracks hazards itself, there is nothing to record
        m_force_memory_sync = true;
    }

    void RHI_CommandList::FlushBarriers()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_force_memory_sync)
            return;

        m_force_memory_sync = false;
        MarkTrackedResourcesSynced();
    }

    void RHI_CommandList::restore_after_external_pass()
    {
        m_pipeline_state_dirty = true;
    }

    void RHI_CommandList::EnsureComputeShaderResource(RHI_Texture* texture, bool include_pixel_stage)
    {
        if (!texture || m_state != RHI_CommandListState::Recording)
            return;

        SetTrackedTextureLayout(texture, 0, texture->GetResidentMipCount(), RHI_Image_Layout::General);
    }

    void RHI_CommandList::AdoptComputeShaderResource(RHI_Texture* texture, bool include_pixel_stage)
    {
        if (!texture || m_state != RHI_CommandListState::Recording)
            return;

        SetTrackedTextureLayout(texture, 0, texture->GetResidentMipCount(), RHI_Image_Layout::General);
        TrackExternalTextureUsage(texture, RHI_Resource_Access::Read, RHI_Image_Layout::General, RHI_Barrier_Scope::Compute);
    }

    void RHI_CommandList::AdoptUnorderedAccess(RHI_Texture* texture)
    {
        if (!texture || m_state != RHI_CommandListState::Recording)
            return;

        SetTrackedTextureLayout(texture, 0, texture->GetResidentMipCount(), RHI_Image_Layout::General);
        TrackExternalTextureUsage(texture, RHI_Resource_Access::Write, RHI_Image_Layout::General, RHI_Barrier_Scope::Compute);
    }

    void RHI_CommandList::InsertBarrier(RHI_Texture* texture, RHI_Image_Layout layout, uint32_t mip, uint32_t mip_range)
    {
        InsertBarrier(RHI_Barrier::image_layout(texture, layout, mip, mip_range));
    }

    void RHI_CommandList::InsertBarrier(RHI_Buffer* buffer)
    {
        InsertBarrier(RHI_Barrier::buffer_sync(buffer));
    }

    void RHI_CommandList::InsertBarrier(void* image, RHI_Format format, uint32_t mip_index, uint32_t mip_range, uint32_t array_length, RHI_Image_Layout layout)
    {
        InsertBarrier(RHI_Barrier::image_layout(image, format, mip_index, mip_range, array_length, layout));
    }

    void RHI_CommandList::RemoveLayout(void* image)
    {
    }

    RHI_Image_Layout RHI_CommandList::GetImageLayout(void* image, const uint32_t mip_index)
    {
        return image ? RHI_Image_Layout::General : RHI_Image_Layout::Max;
    }

    void RHI_CommandList::copy_texture_to_buffer(RHI_Texture* source, RHI_Buffer* destination)
    {
        SP_ASSERT(source && destination);
        render_pass_end();

        WGPUTexture texture_source    = static_cast<WGPUTexture>(source->GetRhiResource());
        WGPUBuffer buffer_destination = static_cast<WGPUBuffer>(destination->GetRhiResource());
        if (!texture_source || !buffer_destination)
            return;

        // texture to buffer copies need 256 byte rows, tight rows go through a padded buffer and are copied over one by one
        const uint32_t width         = source->GetResidentWidth();
        const uint32_t height        = source->GetResidentHeight();
        const uint32_t bytes_per_row = width * source->GetBytesPerPixel();
        const uint32_t padded_row    = (bytes_per_row + 255) & ~255u;
        const bool padded            = padded_row != bytes_per_row;
        if (bytes_per_row % 4 != 0)
        {
            static once_flag logged;
            call_once(logged, []()
            {
                SP_LOG_WARNING("Texture readbacks need rows that are a multiple of 4 bytes on webgpu");
            });
            return;
        }

        State& state = get_state(m_rhi_state);
        end_compute(state);
        PrepareForExternalRead(source, RHI_Image_Layout::General, RHI_Barrier_Scope::Compute);

        WGPUBuffer buffer_copy = buffer_destination;
        if (padded)
        {
            WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
            descriptor.label                = webgpu::label("readback_padded");
            descriptor.size                 = static_cast<uint64_t>(padded_row) * height;
            descriptor.usage                = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
            buffer_copy                     = wgpuDeviceCreateBuffer(RHI_Context::device, &descriptor);
        }

        WGPUTexelCopyTextureInfo copy_source      = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        copy_source.texture                       = texture_source;
        WGPUTexelCopyBufferInfo copy_destination  = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        copy_destination.buffer                   = buffer_copy;
        copy_destination.layout.bytesPerRow       = padded_row;
        copy_destination.layout.rowsPerImage      = height;
        const WGPUExtent3D extent                 = { width, height, 1 };
        wgpuCommandEncoderCopyTextureToBuffer(state.encoder, &copy_source, &copy_destination, &extent);

        if (padded)
        {
            for (uint32_t row = 0; row < height; row++)
            {
                wgpuCommandEncoderCopyBufferToBuffer(state.encoder, buffer_copy, static_cast<uint64_t>(row) * padded_row, buffer_destination, static_cast<uint64_t>(row) * bytes_per_row, bytes_per_row);
            }
            // the command buffer keeps the padded buffer alive until it has run
            wgpuBufferRelease(buffer_copy);
        }
        if (destination->GetMappedData())
        {
            state.readbacks.push_back(destination);
        }
    }

    void RHI_CommandList::copy_buffer_to_buffer(void* source, RHI_Buffer* destination, uint64_t size)
    {
        SP_ASSERT(source && destination);
        render_pass_end();

        State& state = get_state(m_rhi_state);
        end_compute(state);
        wgpuCommandEncoderCopyBufferToBuffer(state.encoder, static_cast<WGPUBuffer>(source), 0, static_cast<WGPUBuffer>(destination->GetRhiResource()), 0, size & ~3ull);
        if (destination->GetType() == RHI_Buffer_Type::Readback)
        {
            state.readbacks.push_back(destination);
        }
    }

    void RHI_CommandList::copy_buffer_to_buffer(RHI_Buffer* source, RHI_Buffer* destination, uint64_t size)
    {
        SP_ASSERT(source);
        get_state(m_rhi_state).touched.insert(source);
        copy_buffer_to_buffer(source->GetRhiResource(), destination, size);
    }

    namespace immediate_execution
    {
        static const uint32_t queue_type_count = static_cast<uint32_t>(RHI_Queue_Type::Max);

        array<mutex, queue_type_count>                 mutexes;
        array<condition_variable, queue_type_count>    condition_vars;
        array<bool, queue_type_count>                  is_executing = {};
        array<shared_ptr<RHI_Queue>, queue_type_count> queues;
        once_flag init_flag;

        void ensure_initialized()
        {
            call_once(init_flag, []()
            {
                queues[static_cast<uint32_t>(RHI_Queue_Type::Graphics)] = make_shared<RHI_Queue>(RHI_Queue_Type::Graphics, "graphics_immediate");
                queues[static_cast<uint32_t>(RHI_Queue_Type::Compute)]  = make_shared<RHI_Queue>(RHI_Queue_Type::Compute,  "compute_immediate");
                queues[static_cast<uint32_t>(RHI_Queue_Type::Copy)]     = make_shared<RHI_Queue>(RHI_Queue_Type::Copy,     "copy_immediate");
            });
        }
    }

    RHI_CommandList* RHI_CommandList::ImmediateExecutionBegin(const RHI_Queue_Type queue_type)
    {
        if (RHI_Device::IsDeviceLost())
            return nullptr;

        immediate_execution::ensure_initialized();

        const uint32_t queue_index = static_cast<uint32_t>(queue_type);
        unique_lock<mutex> lock(immediate_execution::mutexes[queue_index]);
        immediate_execution::condition_vars[queue_index].wait(lock, [queue_index]()
        {
            return !immediate_execution::is_executing[queue_index];
        });
        immediate_execution::is_executing[queue_index] = true;

        RHI_CommandList* cmd_list = immediate_execution::queues[queue_index]->NextCommandList();
        cmd_list->Begin();
        cmd_list->m_previous_binding = RHI_Device::Cmd();
        RHI_Device::Bind(cmd_list);
        return cmd_list;
    }

    void RHI_CommandList::ImmediateExecutionEnd(RHI_CommandList* cmd_list, bool wait)
    {
        if (!cmd_list)
            return;

        cmd_list->Submit(nullptr, wait);

        const uint32_t queue_index = static_cast<uint32_t>(cmd_list->GetQueue()->GetType());

        if (!wait)
        {
            SP_ASSERT(cmd_list->GetQueue()->GetType() == RHI_Queue_Type::Graphics);
            SetPendingUpload(cmd_list->GetWork());
        }

        RHI_Device::Bind(cmd_list->m_previous_binding);

        {
            lock_guard<mutex> lock(immediate_execution::mutexes[queue_index]);
            immediate_execution::is_executing[queue_index] = false;
        }
        immediate_execution::condition_vars[queue_index].notify_one();
    }

    void RHI_CommandList::ImmediateExecutionShutdown()
    {
        for (uint32_t i = 0; i < immediate_execution::queue_type_count; i++)
        {
            unique_lock<mutex> lock(immediate_execution::mutexes[i]);
            immediate_execution::condition_vars[i].wait(lock, [i]()
            {
                return !immediate_execution::is_executing[i];
            });
        }

        immediate_execution::queues.fill(nullptr);
        SetPendingUpload({});
        webgpu_blit::destroy();
    }

    void* RHI_CommandList::GetRhiResourcePipeline()
    {
        return m_pipeline ? m_pipeline->GetRhiResource() : nullptr;
    }
}
