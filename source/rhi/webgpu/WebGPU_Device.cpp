/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ========================
#include "pch.h"
#include "../RHI_Device.h"
#include "../RHI_Queue.h"
#include "../RHI_CommandList.h"
#include "../RHI_Pipeline.h"
#include "../RHI_DescriptorSet.h"
#include "../RHI_Compiler.h"
#include "../RHI_Buffer.h"
#include "../RHI_Texture.h"
#include "../RHI_Sampler.h"
#include "../../profiling/Profiler.h"
#include "WebGPU_Internal.h"
#include <thread>
#include <unordered_set>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#elif defined(_WIN32)
#include <dxgi1_6.h>
#pragma comment(lib, "dxgi.lib")
#endif
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        WGPUQueue native_queue = nullptr;
        unordered_set<uint32_t> features;
        atomic<uint64_t> frame = 0;

        #if defined(_WIN32) && !defined(__EMSCRIPTEN__)
        // dawn::native::d3d::RequestAdapterOptionsLUID, the c header only has its stype
        struct RequestAdapterOptionsLuid
        {
            WGPUChainedStruct chain;
            LUID adapter_luid;
        };

        // dawn never lists the d3d12 software rasterizer (warp) on its own, it has to be asked for by luid
        bool warp_luid(LUID& luid)
        {
            IDXGIFactory4* factory = nullptr;
            if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory))))
                return false;

            IDXGIAdapter1* adapter = nullptr;
            const bool found = SUCCEEDED(factory->EnumWarpAdapter(__uuidof(IDXGIAdapter1), reinterpret_cast<void**>(&adapter)));
            if (found)
            {
                DXGI_ADAPTER_DESC1 desc = {};
                adapter->GetDesc1(&desc);
                luid = desc.AdapterLuid;
                adapter->Release();
            }
            factory->Release();
            return found;
        }
        #endif

        // webgpu doesn't expose video memory, natively dxgi knows it, the browser hides it so report the minimum the engine asks for
        uint64_t adapter_memory(const uint32_t vendor_id, const uint32_t device_id)
        {
            uint64_t memory = 4096ull * 1024 * 1024;
            #if defined(_WIN32) && !defined(__EMSCRIPTEN__)
            IDXGIFactory1* factory = nullptr;
            if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
            {
                IDXGIAdapter1* adapter = nullptr;
                for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
                {
                    DXGI_ADAPTER_DESC1 desc = {};
                    adapter->GetDesc1(&desc);
                    adapter->Release();
                    if (desc.VendorId == vendor_id && desc.DeviceId == device_id)
                    {
                        memory = desc.DedicatedVideoMemory;
                        break;
                    }
                }
                factory->Release();
            }
            #endif
            return memory;
        }

        // limits the log to the first occurrence of every message, validation errors repeat each frame
        void log_error(const WGPUStringView message)
        {
            static mutex log_mutex;
            static unordered_set<string> logged;
            const string text(message.data ? message.data : "", message.data ? (message.length == WGPU_STRLEN ? strlen(message.data) : message.length) : 0);
            lock_guard<mutex> lock(log_mutex);
            if (logged.size() < 512 && logged.insert(text).second)
            {
                SP_LOG_ERROR("WebGPU: %s", text.c_str());
            }
        }

        // async requests finish through the event loop, the browser needs control back to resolve its promises
        void wait_until(const atomic<bool>& done)
        {
            while (!done.load())
            {
                wgpuInstanceProcessEvents(RHI_Context::instance);
                #if defined(__EMSCRIPTEN__)
                emscripten_sleep(1);
                #else
                this_thread::yield();
                #endif
            }
        }
    }

    namespace queues
    {
        array<shared_ptr<RHI_Queue>, static_cast<uint32_t>(RHI_Queue_Type::Max)> regular;

        void destroy()
        {
            regular.fill(nullptr);
        }
    }

    namespace deletion
    {
        struct Entry
        {
            RHI_Resource_Type type;
            void* resource;
        };

        vector<Entry> queue;
        mutex mutex_queue;

        // webgpu objects are reference counted, the implementation keeps them alive while queued work uses them
        void destroy_resource(RHI_Resource_Type type, void* resource)
        {
            if (!resource)
                return;

            switch (type)
            {
                case RHI_Resource_Type::Buffer:
                    wgpuBufferRelease(static_cast<WGPUBuffer>(resource));
                    break;
                case RHI_Resource_Type::Image:
                    wgpuTextureRelease(static_cast<WGPUTexture>(resource));
                    break;
                case RHI_Resource_Type::ImageView:
                    wgpuTextureViewRelease(static_cast<WGPUTextureView>(resource));
                    break;
                case RHI_Resource_Type::Sampler:
                    wgpuSamplerRelease(static_cast<WGPUSampler>(resource));
                    break;
                case RHI_Resource_Type::Shader:
                    webgpu::destroy_shader(resource);
                    break;
                case RHI_Resource_Type::Pipeline:
                    webgpu::destroy_pipeline(resource);
                    break;
                default:
                    break; // descriptor set layouts are hash tokens
            }
        }
    }

    namespace staging
    {
        struct Entry
        {
            WGPUBuffer buffer = nullptr;
            uint64_t size     = 0;
            bool in_use       = false;
        };
        vector<Entry> pool;
        mutex mutex_pool;
    }

    namespace pipelines
    {
        unordered_map<uint64_t, shared_ptr<RHI_Pipeline>> cache;
        mutex mutex_cache;
    }

    namespace dummies
    {
        mutex mutex_dummies;
        WGPUBuffer uniform     = nullptr;
        WGPUBuffer storage     = nullptr;
        WGPUBuffer storage_rw  = nullptr;
        WGPUBuffer zero_vertex = nullptr;
        WGPUTextureView white  = nullptr;
        unordered_map<uint64_t, WGPUTextureView> textures;
        unordered_map<uint64_t, WGPUTextureView> storage_textures;
        array<WGPUSampler, 3> samplers = {};
        WGPUBindGroupLayout layout_push_constants = nullptr;

        const uint64_t storage_size       = 65536;
        const uint64_t storage_rw_regions = 128;
        const uint64_t storage_rw_region  = 16384;

        WGPUBuffer create_buffer(const char* name, const uint64_t size, const WGPUBufferUsage usage)
        {
            WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
            descriptor.label                = webgpu::label(name);
            descriptor.size                 = size;
            descriptor.usage                = usage | WGPUBufferUsage_CopyDst;
            WGPUBuffer buffer               = wgpuDeviceCreateBuffer(RHI_Context::device, &descriptor);
            SP_ASSERT_MSG(buffer != nullptr, "Failed to create a dummy buffer");
            return buffer;
        }

        WGPUTextureView create_texture(const char* name, const WGPUTextureFormat format, const WGPUTextureViewDimension dimension, const WGPUTextureUsage usage, const void* texel)
        {
            const bool cube    = dimension == WGPUTextureViewDimension_Cube || dimension == WGPUTextureViewDimension_CubeArray;
            const bool array   = dimension == WGPUTextureViewDimension_2DArray;
            const uint32_t layers = cube ? 6 : 1;

            WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
            descriptor.label                 = webgpu::label(name);
            descriptor.usage                 = usage | WGPUTextureUsage_CopyDst;
            descriptor.dimension             = dimension == WGPUTextureViewDimension_3D ? WGPUTextureDimension_3D : (dimension == WGPUTextureViewDimension_1D ? WGPUTextureDimension_1D : WGPUTextureDimension_2D);
            descriptor.size                  = { 1, 1, layers };
            descriptor.format                = format;
            descriptor.mipLevelCount         = 1;
            descriptor.sampleCount           = 1;
            WGPUTexture texture              = wgpuDeviceCreateTexture(RHI_Context::device, &descriptor);
            SP_ASSERT_MSG(texture != nullptr, "Failed to create a dummy texture");

            if (texel)
            {
                for (uint32_t layer = 0; layer < layers; layer++)
                {
                    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                    destination.texture                  = texture;
                    destination.origin                   = { 0, 0, layer };
                    WGPUTexelCopyBufferLayout layout     = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
                    layout.bytesPerRow                   = 4;
                    layout.rowsPerImage                  = 1;
                    const WGPUExtent3D size              = { 1, 1, 1 };
                    wgpuQueueWriteTexture(native_queue, &destination, texel, 4, &layout, &size);
                }
            }

            WGPUTextureViewDescriptor view_descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            view_descriptor.dimension                 = dimension;
            view_descriptor.arrayLayerCount           = (cube || array) ? layers : 1;
            view_descriptor.mipLevelCount             = 1;
            WGPUTextureView view                      = wgpuTextureCreateView(texture, &view_descriptor);
            wgpuTextureRelease(texture); // the view keeps it alive
            return view;
        }

        void destroy()
        {
            lock_guard<mutex> lock(mutex_dummies);
            for (WGPUBuffer* buffer : { &uniform, &storage, &storage_rw, &zero_vertex })
            {
                if (*buffer)
                {
                    wgpuBufferRelease(*buffer);
                    *buffer = nullptr;
                }
            }

            for (auto& [key, view] : textures)
            {
                wgpuTextureViewRelease(view);
            }
            textures.clear();

            for (auto& [key, view] : storage_textures)
            {
                wgpuTextureViewRelease(view);
            }
            storage_textures.clear();

            if (white)
            {
                wgpuTextureViewRelease(white);
                white = nullptr;
            }

            for (WGPUSampler& sampler : samplers)
            {
                if (sampler)
                {
                    wgpuSamplerRelease(sampler);
                    sampler = nullptr;
                }
            }

            if (layout_push_constants)
            {
                wgpuBindGroupLayoutRelease(layout_push_constants);
                layout_push_constants = nullptr;
            }
        }
    }

    namespace bindless
    {
        mutex mutex_bindless;
        atomic<uint64_t> generation = 1;
        unordered_map<uint32_t, RHI_Buffer*> buffers;
        array<RHI_Sampler*, 9> samplers = {};

        // null means unchanged, the renderer passes it on frames where nothing was uploaded
        void set_buffer(const uint32_t binding, RHI_Buffer* buffer)
        {
            if (!buffer)
                return;

            lock_guard<mutex> lock(mutex_bindless);
            RHI_Buffer*& current = buffers[binding];
            if (current != buffer)
            {
                current = buffer;
                generation++;
            }
        }
    }

    WGPUQueue webgpu::queue()
    {
        return native_queue;
    }

    void webgpu::process_events()
    {
        if (RHI_Context::instance)
        {
            wgpuInstanceProcessEvents(RHI_Context::instance);
        }
    }

    bool webgpu::has_feature(const WGPUFeatureName feature)
    {
        return features.count(static_cast<uint32_t>(feature)) != 0;
    }

    bool webgpu::is_storage_format(const WGPUTextureFormat format)
    {
        switch (format)
        {
            // core
            case WGPUTextureFormat_RGBA8Unorm:
            case WGPUTextureFormat_RGBA16Float:
            case WGPUTextureFormat_R32Float:
            case WGPUTextureFormat_R32Uint:
            case WGPUTextureFormat_RG32Float:
            case WGPUTextureFormat_RG32Uint:
            case WGPUTextureFormat_RGBA32Float:
                return true;
            // texture formats tier 1
            case WGPUTextureFormat_R8Unorm:
            case WGPUTextureFormat_RG8Unorm:
            case WGPUTextureFormat_R16Float:
            case WGPUTextureFormat_RG16Float:
            case WGPUTextureFormat_R16Uint:
            case WGPUTextureFormat_R8Uint:
            case WGPUTextureFormat_RGB10A2Unorm:
            case WGPUTextureFormat_RG11B10Ufloat:
            case WGPUTextureFormat_R16Unorm:
            case WGPUTextureFormat_RGBA16Unorm:
                return has_feature(WGPUFeatureName_TextureFormatsTier1);
            case WGPUTextureFormat_BGRA8Unorm:
                return has_feature(WGPUFeatureName_BGRA8UnormStorage);
            default:
                return false;
        }
    }

    bool webgpu::is_filterable_format(const WGPUTextureFormat format)
    {
        switch (format)
        {
            case WGPUTextureFormat_R32Float:
            case WGPUTextureFormat_RG32Float:
            case WGPUTextureFormat_RGBA32Float:
                return has_feature(WGPUFeatureName_Float32Filterable);
            default:
                return sample_type_of(format) == WGPUTextureSampleType_Float;
        }
    }

    WGPUTextureSampleType webgpu::sample_type_of(const WGPUTextureFormat format)
    {
        switch (format)
        {
            case WGPUTextureFormat_R8Uint:
            case WGPUTextureFormat_R16Uint:
            case WGPUTextureFormat_R32Uint:
            case WGPUTextureFormat_RG32Uint:
                return WGPUTextureSampleType_Uint;
            case WGPUTextureFormat_Depth16Unorm:
            case WGPUTextureFormat_Depth32Float:
            case WGPUTextureFormat_Depth32FloatStencil8:
                return WGPUTextureSampleType_Depth;
            case WGPUTextureFormat_R32Float:
            case WGPUTextureFormat_RG32Float:
            case WGPUTextureFormat_RGBA32Float:
                return has_feature(WGPUFeatureName_Float32Filterable) ? WGPUTextureSampleType_Float : WGPUTextureSampleType_UnfilterableFloat;
            default:
                return WGPUTextureSampleType_Float;
        }
    }

    uint64_t webgpu::frame_index()
    {
        return frame.load();
    }

    WGPUBindGroupLayout webgpu::push_constants_layout()
    {
        lock_guard<mutex> lock(dummies::mutex_dummies);
        if (!dummies::layout_push_constants)
        {
            WGPUBindGroupLayoutEntry entry   = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            entry.binding                    = 0;
            entry.visibility                 = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
            entry.buffer.type                = WGPUBufferBindingType_Uniform;
            entry.buffer.hasDynamicOffset    = true;
            entry.buffer.minBindingSize      = push_constants_size;

            WGPUBindGroupLayoutDescriptor descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            descriptor.label                         = label("push_constants");
            descriptor.entryCount                    = 1;
            descriptor.entries                       = &entry;
            dummies::layout_push_constants           = wgpuDeviceCreateBindGroupLayout(RHI_Context::device, &descriptor);
        }
        return dummies::layout_push_constants;
    }

    WGPUBuffer webgpu::dummy_uniform_buffer()
    {
        lock_guard<mutex> lock(dummies::mutex_dummies);
        if (!dummies::uniform)
        {
            dummies::uniform = dummies::create_buffer("dummy_uniform", dummy_uniform_size, WGPUBufferUsage_Uniform);
        }
        return dummies::uniform;
    }

    WGPUBuffer webgpu::dummy_storage_buffer(const bool writable, const uint32_t binding, uint64_t& offset, uint64_t& size)
    {
        lock_guard<mutex> lock(dummies::mutex_dummies);
        if (!writable)
        {
            if (!dummies::storage)
            {
                dummies::storage = dummies::create_buffer("dummy_storage", dummies::storage_size, WGPUBufferUsage_Storage);
            }
            offset = 0;
            size   = dummies::storage_size;
            return dummies::storage;
        }

        if (!dummies::storage_rw)
        {
            dummies::storage_rw = dummies::create_buffer("dummy_storage_rw", dummies::storage_rw_regions * dummies::storage_rw_region, WGPUBufferUsage_Storage);
        }
        offset = (binding % dummies::storage_rw_regions) * dummies::storage_rw_region;
        size   = dummies::storage_rw_region;
        return dummies::storage_rw;
    }

    WGPUBuffer webgpu::zero_vertex_buffer()
    {
        lock_guard<mutex> lock(dummies::mutex_dummies);
        if (!dummies::zero_vertex)
        {
            dummies::zero_vertex = dummies::create_buffer("zero_vertex", 256, WGPUBufferUsage_Vertex);
        }
        return dummies::zero_vertex;
    }

    WGPUTextureView webgpu::dummy_texture(const WGPUTextureViewDimension dimension, const WGPUTextureSampleType sample_type)
    {
        lock_guard<mutex> lock(dummies::mutex_dummies);
        const uint64_t key = (static_cast<uint64_t>(dimension) << 32) | static_cast<uint64_t>(sample_type);
        auto it            = dummies::textures.find(key);
        if (it != dummies::textures.end())
            return it->second;

        WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
        if (sample_type == WGPUTextureSampleType_Depth)
        {
            format = WGPUTextureFormat_Depth32Float;
        }
        else if (sample_type == WGPUTextureSampleType_Uint)
        {
            format = WGPUTextureFormat_R32Uint;
        }
        else if (sample_type == WGPUTextureSampleType_Sint)
        {
            format = WGPUTextureFormat_R32Sint;
        }

        const bool depth_3d       = sample_type == WGPUTextureSampleType_Depth && dimension == WGPUTextureViewDimension_3D;
        WGPUTextureView view      = dummies::create_texture("dummy_texture", depth_3d ? WGPUTextureFormat_RGBA8Unorm : format, dimension, WGPUTextureUsage_TextureBinding, nullptr);
        dummies::textures[key]    = view;
        return view;
    }

    WGPUTextureView webgpu::white_texture()
    {
        lock_guard<mutex> lock(dummies::mutex_dummies);
        if (!dummies::white)
        {
            const uint32_t texel = 0xffffffff;
            dummies::white       = dummies::create_texture("white", WGPUTextureFormat_RGBA8Unorm, WGPUTextureViewDimension_2D, WGPUTextureUsage_TextureBinding, &texel);
        }
        return dummies::white;
    }

    WGPUTextureView webgpu::dummy_storage_texture(const WGPUTextureFormat format, const WGPUTextureViewDimension dimension, const uint32_t binding)
    {
        lock_guard<mutex> lock(dummies::mutex_dummies);
        // one per binding, writable storage textures can't alias within a dispatch
        const uint64_t key = (static_cast<uint64_t>(binding) << 40) | (static_cast<uint64_t>(dimension) << 32) | static_cast<uint64_t>(format);
        auto it            = dummies::storage_textures.find(key);
        if (it != dummies::storage_textures.end())
            return it->second;

        WGPUTextureView view           = dummies::create_texture("dummy_storage_texture", format, dimension, WGPUTextureUsage_StorageBinding, nullptr);
        dummies::storage_textures[key] = view;
        return view;
    }

    WGPUSampler webgpu::dummy_sampler(const bool comparison, const bool filtering)
    {
        lock_guard<mutex> lock(dummies::mutex_dummies);
        const uint32_t index = comparison ? 0 : (filtering ? 1 : 2);
        if (!dummies::samplers[index])
        {
            WGPUSamplerDescriptor descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
            descriptor.label                 = label("dummy_sampler");
            if (comparison)
            {
                descriptor.compare = WGPUCompareFunction_Less;
            }
            if (comparison || filtering)
            {
                descriptor.magFilter    = WGPUFilterMode_Linear;
                descriptor.minFilter    = WGPUFilterMode_Linear;
            }
            dummies::samplers[index] = wgpuDeviceCreateSampler(RHI_Context::device, &descriptor);
        }
        return dummies::samplers[index];
    }

    void webgpu::destroy_dummies()
    {
        dummies::destroy();
    }

    uint64_t webgpu::bindless_generation()
    {
        return bindless::generation.load();
    }

    vector<webgpu::BindlessBuffer> webgpu::bindless_buffers()
    {
        lock_guard<mutex> lock(bindless::mutex_bindless);
        vector<BindlessBuffer> result;
        for (const auto& [binding, buffer] : bindless::buffers)
        {
            result.push_back({ buffer, binding });
        }
        return result;
    }

    RHI_Sampler* webgpu::bindless_sampler(const uint32_t index)
    {
        lock_guard<mutex> lock(bindless::mutex_bindless);
        return index < bindless::samplers.size() ? bindless::samplers[index] : nullptr;
    }

    // a sampler that only picks the nearest texel can read unfilterable textures such as depth
    bool webgpu::bindless_sampler_filtering(const uint32_t binding)
    {
        RHI_Sampler* sampler = bindless_sampler(binding - binding_samplers);
        if (!sampler)
            return true;

        return sampler->GetFilterMin() != RHI_Filter::Nearest || sampler->GetFilterMag() != RHI_Filter::Nearest || sampler->GetFilterMipmap() != RHI_Filter::Nearest;
    }

    void webgpu::bindless_invalidate()
    {
        bindless::generation++;
    }

    void webgpu::bindless_reset()
    {
        lock_guard<mutex> lock(bindless::mutex_bindless);
        bindless::buffers.clear();
        bindless::samplers.fill(nullptr);
        bindless::generation++;
    }

    namespace material_layers
    {
        // implicit derivatives pick the source mip that matches the destination mip, so every mip is drawn straight
        // from the source and a 4k texture shrinks without aliasing
        const char* source = R"(
            struct vertex_output
            {
                @builtin(position) position : vec4f,
                @location(0) uv : vec2f,
            };

            @group(0) @binding(0) var source_texture : texture_2d<f32>;
            @group(0) @binding(1) var source_sampler : sampler;

            @vertex fn layer_vs(@builtin(vertex_index) id : u32) -> vertex_output
            {
                var output : vertex_output;
                output.uv       = vec2f(f32((id << 1u) & 2u), f32(id & 2u));
                output.position = vec4f(output.uv * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0), 0.0, 1.0);
                return output;
            }

            @fragment fn layer_ps(input : vertex_output) -> @location(0) vec4f
            {
                return textureSample(source_texture, source_sampler, input.uv);
            }
        )";

        // the layer size trades detail for memory, 64 layers of 1024 or 256 of 512 are both about 360 mb
        const uint32_t layer_size_large   = 1024;
        const uint32_t layer_size_small   = 512;
        const uint32_t layer_count_large  = 64;
        const uint32_t capacity_minimum   = 16;
        const WGPUTextureFormat format    = WGPUTextureFormat_RGBA8Unorm;

        struct Source
        {
            RHI_Texture* texture = nullptr;
            void* srv            = nullptr;
        };

        mutex mutex_layers;
        WGPUShaderModule module       = nullptr;
        WGPUBindGroupLayout layout    = nullptr;
        WGPURenderPipeline pipeline   = nullptr;
        WGPUSampler sampler           = nullptr;
        WGPUTexture texture           = nullptr;
        WGPUTextureView view          = nullptr;
        uint32_t capacity             = 0;
        uint32_t layer_size           = 0;
        uint32_t mip_count            = 0;
        vector<Source> sources;
        bool capacity_warning_logged  = false;

        bool create_pipeline()
        {
            if (pipeline)
                return true;

            WGPUShaderSourceWGSL wgsl             = WGPU_SHADER_SOURCE_WGSL_INIT;
            wgsl.code                             = webgpu::label(source);
            WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
            descriptor.nextInChain                = &wgsl.chain;
            descriptor.label                      = webgpu::label("material_texture_layers");
            module                                = wgpuDeviceCreateShaderModule(RHI_Context::device, &descriptor);

            WGPUSamplerDescriptor sampler_descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
            sampler_descriptor.addressModeU          = WGPUAddressMode_Repeat;
            sampler_descriptor.addressModeV          = WGPUAddressMode_Repeat;
            sampler_descriptor.magFilter             = WGPUFilterMode_Linear;
            sampler_descriptor.minFilter             = WGPUFilterMode_Linear;
            sampler_descriptor.mipmapFilter          = WGPUMipmapFilterMode_Linear;
            sampler                                  = wgpuDeviceCreateSampler(RHI_Context::device, &sampler_descriptor);

            WGPUBindGroupLayoutEntry entries[2] = { WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT };
            entries[0].binding                  = 0;
            entries[0].visibility               = WGPUShaderStage_Fragment;
            entries[0].texture.sampleType       = WGPUTextureSampleType_Float;
            entries[0].texture.viewDimension    = WGPUTextureViewDimension_2D;
            entries[1].binding                  = 1;
            entries[1].visibility               = WGPUShaderStage_Fragment;
            entries[1].sampler.type             = WGPUSamplerBindingType_Filtering;
            WGPUBindGroupLayoutDescriptor layout_descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            layout_descriptor.entryCount                    = 2;
            layout_descriptor.entries                       = entries;
            layout                                          = wgpuDeviceCreateBindGroupLayout(RHI_Context::device, &layout_descriptor);

            WGPUPipelineLayoutDescriptor pipeline_layout_descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
            pipeline_layout_descriptor.bindGroupLayoutCount         = 1;
            pipeline_layout_descriptor.bindGroupLayouts             = &layout;
            WGPUPipelineLayout pipeline_layout                      = wgpuDeviceCreatePipelineLayout(RHI_Context::device, &pipeline_layout_descriptor);

            WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
            target.format               = format;
            WGPUFragmentState fragment  = WGPU_FRAGMENT_STATE_INIT;
            fragment.module             = module;
            fragment.entryPoint         = webgpu::label("layer_ps");
            fragment.targetCount        = 1;
            fragment.targets            = &target;

            WGPURenderPipelineDescriptor pipeline_descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
            pipeline_descriptor.label                        = webgpu::label("material_texture_layers");
            pipeline_descriptor.layout                       = pipeline_layout;
            pipeline_descriptor.vertex.module                = module;
            pipeline_descriptor.vertex.entryPoint            = webgpu::label("layer_vs");
            pipeline_descriptor.primitive.topology           = WGPUPrimitiveTopology_TriangleList;
            pipeline_descriptor.fragment                     = &fragment;
            pipeline                                         = wgpuDeviceCreateRenderPipeline(RHI_Context::device, &pipeline_descriptor);
            wgpuPipelineLayoutRelease(pipeline_layout);
            return pipeline != nullptr;
        }

        void release_texture()
        {
            if (view)
            {
                wgpuTextureViewRelease(view);
                view = nullptr;
            }
            if (texture)
            {
                wgpuTextureRelease(texture);
                texture = nullptr;
            }
            capacity = 0;
            sources.clear();
        }

        void create_texture(const uint32_t layer_count, const uint32_t size)
        {
            release_texture();

            mip_count = 1;
            while ((size >> mip_count) > 0)
            {
                mip_count++;
            }

            WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
            descriptor.label                 = webgpu::label("material_texture_layers");
            descriptor.usage                 = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
            descriptor.dimension             = WGPUTextureDimension_2D;
            descriptor.size                  = { size, size, layer_count };
            descriptor.format                = format;
            descriptor.mipLevelCount         = mip_count;
            descriptor.sampleCount           = 1;
            texture                          = wgpuDeviceCreateTexture(RHI_Context::device, &descriptor);
            if (!texture)
                return;

            WGPUTextureViewDescriptor view_descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            view_descriptor.dimension                 = WGPUTextureViewDimension_2DArray;
            view_descriptor.mipLevelCount             = mip_count;
            view_descriptor.arrayLayerCount           = layer_count;
            view                                      = wgpuTextureCreateView(texture, &view_descriptor);
            capacity                                  = layer_count;
            layer_size                                = size;
            sources.assign(layer_count, Source());
        }

        // a null source fills the layer with white, what a material without that texture would read
        void draw_layer(WGPUCommandEncoder encoder, const uint32_t layer, WGPUTextureView source_view)
        {
            WGPUBindGroup group = nullptr;
            if (source_view)
            {
                WGPUBindGroupEntry entries[2] = { WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT };
                entries[0].binding            = 0;
                entries[0].textureView        = source_view;
                entries[1].binding            = 1;
                entries[1].sampler            = sampler;
                WGPUBindGroupDescriptor group_descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
                group_descriptor.layout                  = layout;
                group_descriptor.entryCount              = 2;
                group_descriptor.entries                 = entries;
                group                                    = wgpuDeviceCreateBindGroup(RHI_Context::device, &group_descriptor);
            }

            for (uint32_t mip = 0; mip < mip_count; mip++)
            {
                WGPUTextureViewDescriptor view_descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
                view_descriptor.dimension                 = WGPUTextureViewDimension_2D;
                view_descriptor.baseMipLevel              = mip;
                view_descriptor.mipLevelCount             = 1;
                view_descriptor.baseArrayLayer            = layer;
                view_descriptor.arrayLayerCount           = 1;
                WGPUTextureView destination               = wgpuTextureCreateView(texture, &view_descriptor);

                WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                attachment.view                          = destination;
                attachment.loadOp                        = WGPULoadOp_Clear;
                attachment.storeOp                       = WGPUStoreOp_Store;
                attachment.clearValue                    = { 1.0, 1.0, 1.0, 1.0 };
                WGPURenderPassDescriptor pass_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                pass_descriptor.label                    = webgpu::label("material_texture_layer");
                pass_descriptor.colorAttachmentCount     = 1;
                pass_descriptor.colorAttachments         = &attachment;

                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);
                if (group)
                {
                    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
                    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
                    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
                }
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
                wgpuTextureViewRelease(destination);
            }

            if (group)
            {
                wgpuBindGroupRelease(group);
            }
        }

        // only filterable 2d textures can be resampled, anything else reads white like it did before
        WGPUTextureView source_view_of(RHI_Texture* source)
        {
            if (!source || source->GetType() != RHI_Texture_Type::Type2D)
                return nullptr;

            if (webgpu::sample_type_of(webgpu::texture_format(source)) != WGPUTextureSampleType_Float)
                return nullptr;

            return static_cast<WGPUTextureView>(source->GetRhiSrv());
        }

        void update(const array<RHI_Texture*, rhi_max_array_size>& textures)
        {
            uint32_t count = 0;
            for (uint32_t i = 0; i < rhi_max_array_size; i++)
            {
                if (textures[i])
                {
                    count = i + 1;
                }
            }

            lock_guard<mutex> lock(mutex_layers);
            if (count == 0 || !create_pipeline())
                return;

            WGPULimits limits = WGPU_LIMITS_INIT;
            wgpuDeviceGetLimits(RHI_Context::device, &limits);
            const uint32_t layer_limit = max(limits.maxTextureArrayLayers, 1u);
            if (count > layer_limit && !capacity_warning_logged)
            {
                SP_LOG_WARNING("%u material textures but only %u texture array layers, the rest read the last layer", count, layer_limit);
                capacity_warning_logged = true;
            }
            const uint32_t needed = min(count, layer_limit);

            // growing redraws everything, the capacity doubles so a world that streams materials in rarely pays for it
            const uint32_t size = needed <= layer_count_large ? layer_size_large : layer_size_small;
            if (!texture || needed > capacity || size != layer_size)
            {
                uint32_t layer_count = capacity_minimum;
                while (layer_count < needed)
                {
                    layer_count *= 2;
                }
                create_texture(min(layer_count, layer_limit), size);
                webgpu::bindless_invalidate();
                if (!texture)
                {
                    SP_LOG_ERROR("Failed to create the material texture layers");
                    return;
                }
            }

            WGPUCommandEncoder encoder = nullptr;
            for (uint32_t layer = 0; layer < needed; layer++)
            {
                RHI_Texture* source = textures[layer];
                void* srv           = source ? source->GetRhiSrv() : nullptr;
                if (sources[layer].texture == source && sources[layer].srv == srv)
                    continue;

                // the renderer retries until a texture has a view, the layer keeps what it had until then
                if (source && !srv)
                    continue;

                if (!encoder)
                {
                    WGPUCommandEncoderDescriptor descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
                    descriptor.label                        = webgpu::label("material_texture_layers");
                    encoder                                 = wgpuDeviceCreateCommandEncoder(RHI_Context::device, &descriptor);
                }
                draw_layer(encoder, layer, source_view_of(source));
                sources[layer] = { source, srv };
            }

            if (encoder)
            {
                WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
                wgpuQueueSubmit(webgpu::queue(), 1, &commands);
                wgpuCommandBufferRelease(commands);
                wgpuCommandEncoderRelease(encoder);
            }
        }

        void destroy()
        {
            lock_guard<mutex> lock(mutex_layers);
            release_texture();
            layer_size = 0;
            if (pipeline)
            {
                wgpuRenderPipelineRelease(pipeline);
                pipeline = nullptr;
            }
            if (layout)
            {
                wgpuBindGroupLayoutRelease(layout);
                layout = nullptr;
            }
            if (module)
            {
                wgpuShaderModuleRelease(module);
                module = nullptr;
            }
            if (sampler)
            {
                wgpuSamplerRelease(sampler);
                sampler = nullptr;
            }
        }
    }

    WGPUTextureView webgpu::material_texture_layers()
    {
        lock_guard<mutex> lock(material_layers::mutex_layers);
        return material_layers::view;
    }

    void webgpu::material_texture_layers_destroy()
    {
        material_layers::destroy();
    }

    void RHI_Device::Initialize()
    {
        // instance
        {
            WGPUInstanceDescriptor descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
            RHI_Context::instance             = wgpuCreateInstance(&descriptor);
            SP_ASSERT_MSG(RHI_Context::instance != nullptr, "Failed to create a WebGPU instance");
        }

        // adapter, a machine without a gpu (ci) falls back to a software one (warp natively)
        {
            const bool software_only = Engine::HasArgument("--software-gpu");
            for (const bool fallback : { false, true })
            {
                if ((software_only && !fallback) || RHI_Context::adapter)
                    continue;

                WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
                options.powerPreference           = WGPUPowerPreference_HighPerformance;
                #if defined(_WIN32) && !defined(__EMSCRIPTEN__)
                RequestAdapterOptionsLuid luid_options = {};
                luid_options.chain.sType               = WGPUSType_RequestAdapterOptionsLUID;
                if (fallback)
                {
                    if (!warp_luid(luid_options.adapter_luid))
                        continue;
                    options.nextInChain = &luid_options.chain;
                }
                #else
                options.forceFallbackAdapter = fallback ? WGPU_TRUE : WGPU_FALSE;
                #endif
                atomic<bool> done = false;
                WGPURequestAdapterCallbackInfo callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
                callback.mode                           = WGPUCallbackMode_AllowProcessEvents;
                callback.userdata1                      = &done;
                callback.callback                       = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void* userdata2)
                {
                    if (status == WGPURequestAdapterStatus_Success)
                    {
                        RHI_Context::adapter = adapter;
                    }
                    else
                    {
                        log_error(message);
                    }
                    static_cast<atomic<bool>*>(userdata1)->store(true);
                };
                wgpuInstanceRequestAdapter(RHI_Context::instance, &options, callback);
                wait_until(done);
            }
            SP_ASSERT_MSG(RHI_Context::adapter != nullptr, "No WebGPU adapter is available");
        }

        // physical device
        WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
        wgpuAdapterGetInfo(RHI_Context::adapter, &info);
        const string adapter_name = info.device.data ? string(info.device.data, info.device.length == WGPU_STRLEN ? strlen(info.device.data) : info.device.length) : "WebGPU adapter";
        {
            const RHI_PhysicalDevice_Type type = info.adapterType == WGPUAdapterType_DiscreteGPU ? RHI_PhysicalDevice_Type::Discrete :
                                                 info.adapterType == WGPUAdapterType_IntegratedGPU ? RHI_PhysicalDevice_Type::Integrated :
                                                 info.adapterType == WGPUAdapterType_CPU ? RHI_PhysicalDevice_Type::Cpu : RHI_PhysicalDevice_Type::Virtual;
            PhysicalDeviceRegister(RHI_PhysicalDevice(
                1,
                0,
                nullptr,
                info.vendorID,
                type,
                adapter_name.c_str(),
                adapter_memory(info.vendorID, info.deviceID),
                RHI_Context::adapter
            ));
            PhysicalDeviceSetPrimary(0);
        }

        // the defaults are what every browser guarantees, the engine binds far more per stage, so ask for what the adapter has
        WGPULimits limits = WGPU_LIMITS_INIT;
        wgpuAdapterGetLimits(RHI_Context::adapter, &limits);
        limits.nextInChain = nullptr;

        // features, only the ones the adapter has, the rest degrade
        vector<WGPUFeatureName> required;
        {
            const WGPUFeatureName wanted[] =
            {
                WGPUFeatureName_Float32Filterable,
                WGPUFeatureName_Float32Blendable,
                WGPUFeatureName_TextureFormatsTier1,
                WGPUFeatureName_TextureFormatsTier2,
                WGPUFeatureName_Unorm16TextureFormats,
                WGPUFeatureName_RG11B10UfloatRenderable,
                WGPUFeatureName_BGRA8UnormStorage,
                WGPUFeatureName_Depth32FloatStencil8,
                WGPUFeatureName_TextureCompressionBC,
                WGPUFeatureName_IndirectFirstInstance,
                WGPUFeatureName_DepthClipControl,
                WGPUFeatureName_DualSourceBlending,
                #if !defined(__EMSCRIPTEN__)
                WGPUFeatureName_ImplicitDeviceSynchronization, // shaders and pipelines are created on worker threads
                #endif
            };

            for (WGPUFeatureName feature : wanted)
            {
                if (wgpuAdapterHasFeature(RHI_Context::adapter, feature))
                {
                    required.push_back(feature);
                    features.insert(static_cast<uint32_t>(feature));
                }
            }
        }

        // device
        {
            WGPUDeviceDescriptor descriptor   = WGPU_DEVICE_DESCRIPTOR_INIT;
            descriptor.label                  = webgpu::label("device");
            descriptor.requiredFeatureCount   = required.size();
            descriptor.requiredFeatures       = required.data();
            descriptor.requiredLimits         = &limits;
            descriptor.defaultQueue.label     = webgpu::label("queue");
            descriptor.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const* device, WGPUErrorType type, WGPUStringView message, void* userdata1, void* userdata2)
            {
                log_error(message);
            };
            descriptor.deviceLostCallbackInfo.mode     = WGPUCallbackMode_AllowSpontaneous;
            descriptor.deviceLostCallbackInfo.callback = [](WGPUDevice const* device, WGPUDeviceLostReason reason, WGPUStringView message, void* userdata1, void* userdata2)
            {
                if (reason != WGPUDeviceLostReason_Destroyed && reason != WGPUDeviceLostReason_CallbackCancelled)
                {
                    log_error(message);
                    RHI_Device::SetDeviceLost();
                }
            };

            atomic<bool> done = false;
            WGPURequestDeviceCallbackInfo callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
            callback.mode                          = WGPUCallbackMode_AllowProcessEvents;
            callback.userdata1                     = &done;
            callback.callback                      = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void* userdata2)
            {
                if (status == WGPURequestDeviceStatus_Success)
                {
                    RHI_Context::device = device;
                }
                else
                {
                    log_error(message);
                }
                static_cast<atomic<bool>*>(userdata1)->store(true);
            };
            wgpuAdapterRequestDevice(RHI_Context::adapter, &descriptor, callback);
            wait_until(done);
            SP_ASSERT_MSG(RHI_Context::device != nullptr, "Failed to create a WebGPU device");
        }
        native_queue = wgpuDeviceGetQueue(RHI_Context::device);

        // limits
        m_max_texture_2d_dimension                 = limits.maxTextureDimension2D;
        m_max_push_constant_size                   = webgpu::push_constants_size;
        m_min_uniform_buffer_offset_alignment      = limits.minUniformBufferOffsetAlignment;
        m_min_storage_buffer_offset_alignment      = limits.minStorageBufferOffsetAlignment;
        m_min_acceleration_buffer_offset_alignment = 256;
        m_optimal_buffer_copy_offset_alignment     = 256;
        m_shader_group_handle_size                 = 32;
        m_shader_group_handle_alignment            = 32;
        m_shader_group_base_alignment              = 64;
        m_timestamp_period                         = 1.0f;

        // webgpu has neither ray tracing, mesh shaders nor shading rate images
        m_is_ray_tracing_supported          = false;
        m_is_mesh_shaders_supported         = false;
        m_is_shading_rate_supported         = false;
        m_is_shader_base_instance_supported = false;
        m_xess_supported                    = false;
        m_dlss_supported                    = false;

        queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Graphics)] = make_shared<RHI_Queue>(RHI_Queue_Type::Graphics, "graphics");
        queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Compute)]  = make_shared<RHI_Queue>(RHI_Queue_Type::Compute,  "compute");
        queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Copy)]     = make_shared<RHI_Queue>(RHI_Queue_Type::Copy,     "copy");
        queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Present)]  = make_shared<RHI_Queue>(RHI_Queue_Type::Present,  "present");

        static const char* backends[] = { "undefined", "null", "webgpu", "d3d11", "d3d12", "metal", "vulkan", "opengl", "opengles" };
        RHI_Context::api_version_cstr = static_cast<uint32_t>(info.backendType) < size(backends) ? backends[info.backendType] : "unknown";
        SP_LOG_INFO("WebGPU initialized successfully (%s, %s backend)", adapter_name.c_str(), RHI_Context::api_version_cstr);
        wgpuAdapterInfoFreeMembers(info);
    }

    RHI_TimestampCalibration RHI_Device::GetTimestampCalibration(RHI_Queue_Type type)
    {
        // timestamp queries aren't requested, gpu timing stays empty
        RHI_TimestampCalibration result;
        result.period_ns  = 1.0;
        result.calibrated = false;
        return result;
    }

    void RHI_Device::Tick(const uint64_t frame_count)
    {
        frame = frame_count;
        webgpu::process_events();
        DeletionQueueParse();
    }

    void RHI_Device::Destroy()
    {
        RHI_Compiler::Shaders().Flush();
        RHI_Compiler::Pipelines().Flush();
        DestroySwapChain();
        QueueWaitAll();

        DeletionQueueParse();
        {
            lock_guard<mutex> lock(pipelines::mutex_cache);
            pipelines::cache.clear();
        }
        queues::destroy();
        StagingBufferPoolDestroy();
        DeletionQueueFlush();
        webgpu::bindless_reset();
        webgpu::material_texture_layers_destroy();
        webgpu::destroy_dummies();

        if (native_queue)
        {
            wgpuQueueRelease(native_queue);
            native_queue = nullptr;
        }

        wgpuDeviceDestroy(RHI_Context::device);
        wgpuDeviceRelease(RHI_Context::device);
        RHI_Context::device = nullptr;
        wgpuAdapterRelease(RHI_Context::adapter);
        RHI_Context::adapter = nullptr;
        wgpuInstanceRelease(RHI_Context::instance);
        RHI_Context::instance = nullptr;
    }

    void RHI_Device::QueueWaitAll(const bool flush)
    {
        static mutex wait_mutex;
        lock_guard<mutex> lock(wait_mutex);

        for (const shared_ptr<RHI_Queue>& queue : queues::regular)
        {
            if (queue)
            {
                queue->Wait(flush);
            }
        }
    }

    uint32_t RHI_Device::GetQueueIndex(const RHI_Queue_Type type)
    {
        return 0;
    }

    bool RHI_Device::IsAsyncComputeConcurrent()
    {
        return false;
    }

    RHI_Queue* RHI_Device::GetQueue(const RHI_Queue_Type type)
    {
        return queues::regular[static_cast<uint32_t>(type)].get();
    }

    void* RHI_Device::GetQueueRhiResource(const RHI_Queue_Type type)
    {
        return native_queue;
    }

    void RHI_Device::DeletionQueueAdd(const RHI_Resource_Type resource_type, void* resource)
    {
        if (!resource)
            return;

        lock_guard<mutex> guard(deletion::mutex_queue);
        deletion::queue.push_back({ resource_type, resource });
    }

    void RHI_Device::DeletionQueueParse()
    {
        vector<deletion::Entry> retired;
        {
            lock_guard<mutex> guard(deletion::mutex_queue);
            retired.swap(deletion::queue);
        }

        // destroying can enqueue more deletions, so never under the lock
        for (const deletion::Entry& entry : retired)
        {
            deletion::destroy_resource(entry.type, entry.resource);
        }
    }

    void RHI_Device::DeletionQueueFlush()
    {
        do
        {
            DeletionQueueParse();
        } while (DeletionQueueNeedsToParse());
    }

    bool RHI_Device::DeletionQueueNeedsToParse()
    {
        lock_guard<mutex> guard(deletion::mutex_queue);
        return !deletion::queue.empty();
    }

    void* RHI_Device::MemoryGetMappedDataFromBuffer(void* resource)
    {
        return nullptr;
    }

    void RHI_Device::MemoryBufferCreate(void*& resource, const uint64_t size, uint32_t flags_usage, uint32_t flags_memory, const void* data, const char* name, const bool transient, const bool allow_failure)
    {
        SP_ASSERT_MSG(false, "webgpu buffers allocate in RHI_Buffer::RHI_CreateResource");
    }

    void RHI_Device::MemoryBufferDestroy(void*& resource)
    {
        if (resource)
        {
            DeletionQueueAdd(RHI_Resource_Type::Buffer, resource);
            resource = nullptr;
        }
    }

    void RHI_Device::MemoryTextureCreate(RHI_Texture* texture)
    {
        SP_ASSERT_MSG(false, "webgpu textures allocate in RHI_Texture::RHI_CreateResource");
    }

    void RHI_Device::MemoryTextureDestroy(void*& resource)
    {
        if (resource)
        {
            DeletionQueueAdd(RHI_Resource_Type::Image, resource);
            resource = nullptr;
        }
    }

    void RHI_Device::MemoryMap(void* resource, void*& mapped_data)
    {
        mapped_data = nullptr;
    }

    void RHI_Device::MemoryUnmap(void* resource)
    {
    }

    // webgpu doesn't report memory, the browser hides it on purpose
    uint64_t RHI_Device::MemoryGetAllocatedMb()
    {
        return 0;
    }

    uint64_t RHI_Device::MemoryGetAvailableMb()
    {
        return 4096;
    }

    uint64_t RHI_Device::MemoryGetTotalMb()
    {
        return 4096;
    }

    // staging buffers are copy sources, the queue writes their content in RHI_CommandList::update_buffer
    void* RHI_Device::StagingBufferAcquire(uint64_t size)
    {
        lock_guard<mutex> lock(staging::mutex_pool);

        for (staging::Entry& entry : staging::pool)
        {
            if (!entry.in_use && entry.size >= size)
            {
                entry.in_use = true;
                return entry.buffer;
            }
        }

        WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
        descriptor.label                = webgpu::label("staging");
        descriptor.size                 = (max<uint64_t>(size, 4) + 3) & ~3ull;
        descriptor.usage                = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer               = wgpuDeviceCreateBuffer(RHI_Context::device, &descriptor);
        if (!buffer)
            return nullptr;

        staging::pool.push_back({ buffer, descriptor.size, true });
        return buffer;
    }

    void RHI_Device::StagingBufferRelease(void* buffer)
    {
        lock_guard<mutex> lock(staging::mutex_pool);
        for (staging::Entry& entry : staging::pool)
        {
            if (entry.buffer == buffer)
            {
                entry.in_use = false;
                return;
            }
        }
    }

    void RHI_Device::StagingBufferPoolDestroy()
    {
        lock_guard<mutex> lock(staging::mutex_pool);
        for (staging::Entry& entry : staging::pool)
        {
            wgpuBufferRelease(entry.buffer);
        }
        staging::pool.clear();
    }

    void RHI_Device::GetOrCreatePipeline(RHI_PipelineState& pso, RHI_Pipeline*& pipeline, RHI_DescriptorSetLayout*& descriptor_set_layout)
    {
        SP_ASSERT(pso.GetHash() != 0);
        lock_guard<mutex> lock(pipelines::mutex_cache);

        descriptor_set_layout = nullptr;
        uint64_t hash         = pso.GetHash();
        auto it               = pipelines::cache.find(hash);
        if (it == pipelines::cache.end())
        {
            it = pipelines::cache.emplace(make_pair(hash, make_shared<RHI_Pipeline>(pso, descriptor_set_layout))).first;
        }

        pipeline = it->second->IsReady() && it->second->GetRhiResource() ? it->second.get() : nullptr;
    }

    uint32_t RHI_Device::GetPipelineCount()
    {
        lock_guard<mutex> lock(pipelines::mutex_cache);
        return static_cast<uint32_t>(pipelines::cache.size());
    }

    uint32_t RHI_Device::GetPipelineCompilingCount()
    {
        lock_guard<mutex> lock(pipelines::mutex_cache);
        uint32_t count = 0;
        for (const auto& [hash, pipeline] : pipelines::cache)
        {
            count += pipeline && !pipeline->IsReady() ? 1 : 0;
        }
        return count;
    }

    void* RHI_Device::GetPipelineCache()
    {
        return nullptr;
    }

    void RHI_Device::SetResourceName(void* resource, const RHI_Resource_Type resource_type, const char* name)
    {
        if (!resource || !name)
            return;

        if (resource_type == RHI_Resource_Type::Buffer)
        {
            wgpuBufferSetLabel(static_cast<WGPUBuffer>(resource), webgpu::label(name));
        }
        else if (resource_type == RHI_Resource_Type::Image)
        {
            wgpuTextureSetLabel(static_cast<WGPUTexture>(resource), webgpu::label(name));
        }
    }

    // markers go into the encoders, see begin_marker in WebGPU_CommandList.cpp
    void RHI_Device::MarkerBegin(RHI_CommandList* cmd_list, const char* name, const math::Vector4& color)
    {
    }

    void RHI_Device::MarkerEnd(RHI_CommandList* cmd_list)
    {
    }

    void RHI_Device::SetVariableRateShading(const RHI_CommandList* cmd_list, const bool enabled)
    {
    }

    void* RHI_Device::GetDescriptorSet(const RHI_Device_Bindless_Resource resource_type)
    {
        return nullptr;
    }

    void* RHI_Device::GetDescriptorSetLayout(const RHI_Device_Bindless_Resource resource_type)
    {
        return nullptr;
    }

    uint32_t RHI_Device::GetDescriptorType(const RHI_Descriptor& descriptor)
    {
        return 0;
    }

    void RHI_Device::AllocateDescriptorSet(void*& resource, RHI_DescriptorSetLayout* descriptor_set_layout, const vector<RHI_DescriptorWithBinding>& descriptors)
    {
        // bind groups are created per draw, there are no persistent descriptor sets
    }

    unordered_map<uint64_t, RHI_DescriptorSet>& RHI_Device::GetDescriptorSets()
    {
        static unordered_map<uint64_t, RHI_DescriptorSet> descriptors;
        return descriptors;
    }

    mutex& RHI_Device::GetDescriptorSetMutex()
    {
        static mutex descriptor_mutex;
        return descriptor_mutex;
    }

    uint64_t RHI_Device::GetDescriptorSetFrame()
    {
        return 0;
    }

    void RHI_Device::DescriptorSetInvalidateReferencingResource(void* resource)
    {
    }

    uint64_t RHI_Device::GetBufferDeviceAddress(void* buffer)
    {
        return 0;
    }

    // bindless, every pipeline builds its own group 1 out of these, see bind_bindless in WebGPU_CommandList.cpp
    void RHI_Device::UpdateBindlessMaterials(array<RHI_Texture*, rhi_max_array_size>* textures, RHI_Buffer* parameters)
    {
        if (textures)
        {
            material_layers::update(*textures);
        }
        bindless::set_buffer(316, parameters);
    }

    void RHI_Device::UpdateBindlessSamplers(const shared_ptr<RHI_Sampler>* samplers, uint32_t count)
    {
        if (!samplers)
            return;

        lock_guard<mutex> lock(bindless::mutex_bindless);
        for (uint32_t i = 0; i < count && i < bindless::samplers.size(); i++)
        {
            bindless::samplers[i] = samplers[i].get();
        }
        bindless::generation++;
    }

    void RHI_Device::UpdateBindlessLights(RHI_Buffer* parameters)
    {
        bindless::set_buffer(317, parameters);
    }

    void RHI_Device::UpdateBindlessAABBs(RHI_Buffer* buffer)
    {
        bindless::set_buffer(318, buffer);
    }

    void RHI_Device::UpdateBindlessDrawData(RHI_Buffer* buffer)
    {
        bindless::set_buffer(319, buffer);
    }

    void RHI_Device::UpdateBindlessGeometryVertices(RHI_Buffer* buffer)
    {
        bindless::set_buffer(320, buffer);
    }

    void RHI_Device::UpdateBindlessGeometryIndices(RHI_Buffer* buffer)
    {
        bindless::set_buffer(322, buffer);
    }

    void RHI_Device::UpdateBindlessInstances(RHI_Buffer* buffer)
    {
        bindless::set_buffer(323, buffer);
    }
}
